"""Offline safety/restore tests. No real cloud keys or cloud requests are used."""
from dataclasses import replace
import hashlib
import importlib.util
import json
from pathlib import Path
import sqlite3
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location("qiniu_backup", Path(__file__).with_name("backup.py"))
backup = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = backup
spec.loader.exec_module(backup)


class BackupTests(unittest.TestCase):
    def setUp(self):
        test_root = Path(__file__).with_name("backups")
        test_root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="test-", dir=test_root)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.sources = {}
        for label in ("blog", "artalk"):
            path = self.root / f"{label}.db"
            with sqlite3.connect(path) as conn:
                conn.execute("CREATE TABLE messages (body TEXT)")
                conn.execute("INSERT INTO messages VALUES (?)", (label,))
            conn.close()
            self.sources[label] = path
        self.settings = backup.Settings(
            self.sources, self.root / "backups", 2, 10,
            "private-backups", "blog-databases", self.root / "credentials.ini",
        )

    def archive(self):
        with backup.job_lock(self.settings.output_dir):
            return backup.create_archive(self.settings)

    def test_restore_includes_committed_wal_and_no_credentials(self):
        live = sqlite3.connect(self.sources["blog"])
        try:
            live.execute("PRAGMA journal_mode=WAL")
            live.execute("PRAGMA wal_autocheckpoint=0")
            live.execute("INSERT INTO messages VALUES ('committed in WAL')")
            live.commit()
            self.assertGreater(Path(str(self.sources["blog"]) + "-wal").stat().st_size, 0)
            (self.root / "credentials.ini").write_text("SECRET-DO-NOT-UPLOAD", encoding="utf-8")
            archive = self.archive()
            with tarfile.open(archive) as bundle:
                self.assertEqual(set(bundle.getnames()), {"blog.db", "artalk.db", "manifest.json"})
                manifest = json.load(bundle.extractfile("manifest.json"))
                for label in ("blog", "artalk"):
                    data = bundle.extractfile(f"{label}.db").read()
                    self.assertNotIn(b"SECRET-DO-NOT-UPLOAD", data)
                    self.assertEqual(hashlib.sha256(data).hexdigest(), manifest["databases"][label]["sha256"])
                    restored = self.root / f"restored-{label}.db"
                    restored.write_bytes(data)
                    conn = sqlite3.connect(restored)
                    try:
                        self.assertEqual(conn.execute("PRAGMA integrity_check").fetchone(), ("ok",))
                        rows = conn.execute("SELECT body FROM messages").fetchall()
                        self.assertIn((label,), rows)
                        if label == "blog":
                            self.assertIn(("committed in WAL",), rows)
                    finally:
                        conn.close()
            self.assertEqual(live.execute("SELECT COUNT(*) FROM messages").fetchone(), (2,))
        finally:
            live.close()

    def test_missing_source_is_not_created_and_no_partial_archive_remains(self):
        missing = self.root / "missing.db"
        settings = replace(self.settings, databases={"blog": self.sources["blog"], "artalk": missing})
        with backup.job_lock(settings.output_dir):
            with self.assertRaises(sqlite3.OperationalError):
                backup.create_archive(settings)
        self.assertFalse(missing.exists())
        self.assertEqual(list(settings.output_dir.glob("*.tar.gz*")), [])
        self.assertEqual(list(settings.output_dir.glob("snapshot-*")), [])

    def test_corrupt_source_aborts_entire_bundle(self):
        self.sources["artalk"].write_bytes(b"not a database")
        with self.assertRaises(sqlite3.DatabaseError):
            self.archive()
        self.assertEqual(list(self.settings.output_dir.glob("*.tar.gz*")), [])

    def test_concurrent_run_is_rejected(self):
        with backup.job_lock(self.settings.output_dir):
            with self.assertRaises(backup.BackupError):
                with backup.job_lock(self.settings.output_dir):
                    self.fail("second run acquired lock")

    def test_relative_paths_ignore_current_working_directory(self):
        cfg = self.root / "backup.ini"
        cfg.write_text("[backup]\noutput_dir=backups\n[databases]\nblog=blog.db\nartalk=artalk.db\n"
                       "[qiniu]\nbucket=private-backups\ncredentials_file=credentials.ini\n", encoding="utf-8")
        loaded = backup.load_settings(cfg)
        self.assertEqual(loaded.databases, self.sources)
        self.assertEqual(loaded.output_dir, self.root / "backups")
        self.assertEqual(loaded.credentials_file, self.root / "credentials.ini")

    def client(self, remote_hash="expected", remote_size=None):
        sdk = SimpleNamespace(etag=Mock(return_value="expected"), put_file=Mock(return_value=(None, None)))
        auth = Mock()
        manager = Mock()
        manager.stat.return_value = ({"hash": remote_hash, "fsize": remote_size}, SimpleNamespace(status_code=200))
        return sdk, auth, manager

    def test_upload_is_insert_only_and_verified_by_hash_and_size(self):
        archive = self.archive()
        client = self.client(remote_size=archive.stat().st_size)
        key = backup.upload_archive(self.settings, archive, client)
        self.assertTrue(key.startswith("blog-databases/20"))
        self.assertTrue(key.endswith(archive.name))
        self.assertEqual(client[1].upload_token.call_args.args[-1], {"insertOnly": 1})
        self.assertTrue(client[0].put_file.call_args.kwargs["check_crc"])
        self.assertIs(client[0].put_file.call_args.kwargs["regions"], client[2].regions)

    def test_upload_mismatch_retries_fails_and_retains_archive(self):
        archive = self.archive()
        client = self.client(remote_hash="wrong", remote_size=archive.stat().st_size)
        with patch.object(backup.time, "sleep"):
            with self.assertRaises(backup.BackupError):
                backup.upload_archive(self.settings, archive, client)
        self.assertEqual(client[0].put_file.call_count, 3)
        self.assertTrue(archive.exists())

    def test_sdk_exception_does_not_reach_logs(self):
        archive = self.archive()
        client = self.client()
        client[0].put_file.side_effect = RuntimeError("SECRET-AUTH-TOKEN")
        with patch.object(backup.time, "sleep"), self.assertLogs("qiniu-backup") as logs:
            with self.assertRaises(backup.BackupError):
                backup.upload_archive(self.settings, archive, client)
        self.assertNotIn("SECRET-AUTH-TOKEN", " ".join(logs.output))

    def test_public_bucket_is_rejected(self):
        self.settings.credentials_file.write_text("[qiniu]\naccess_key=fake\nsecret_key=fake\n", encoding="utf-8")
        self.settings.credentials_file.chmod(0o600)
        sdk = SimpleNamespace(Zone=Mock(), set_default=Mock(), Auth=Mock(), BucketManager=Mock())
        sdk.BucketManager.return_value.bucket_info.return_value = ({"private": 0}, SimpleNamespace(status_code=200))
        with patch.dict(sys.modules, {"qiniu": sdk}), self.assertRaises(backup.BackupError):
            backup.qiniu_client(self.settings)
        self.assertEqual(sdk.Zone.call_args.kwargs["scheme"], "https")

    def test_retention_preserves_failed_backups_and_unrelated_files(self):
        paths = [self.archive() for _ in range(4)]
        for archive in paths[:3]:
            archive.with_suffix(archive.suffix + ".uploaded").touch()
        unrelated = self.settings.output_dir / "unrelated.tar.gz"
        unrelated.touch()
        backup.cleanup_uploaded(self.settings)
        self.assertFalse(paths[0].exists())
        self.assertTrue(all(path.exists() for path in paths[1:]))
        self.assertTrue(unrelated.exists())


if __name__ == "__main__":
    unittest.main()
