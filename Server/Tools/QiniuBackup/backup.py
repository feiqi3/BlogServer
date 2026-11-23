#!/usr/bin/env python3
"""Consistent SQLite snapshots of the blog and Artalk, uploaded to private Kodo."""
from __future__ import annotations

import argparse
from contextlib import closing, contextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import sqlite3
import sys
import tarfile
import tempfile
import time
import uuid
import configparser

LOG = logging.getLogger("qiniu-backup")
ARCHIVE_PATTERN = re.compile(r"blog-backup-\d{8}T\d{12}Z-[0-9a-f]{8}\.tar\.gz\Z")


class BackupError(Exception):
    """A controlled error whose message contains no credential or SDK response."""


@dataclass(frozen=True)
class Settings:
    databases: dict[str, Path]
    output_dir: Path
    local_keep: int
    snapshot_timeout: int
    bucket: str
    prefix: str
    credentials_file: Path


def read_ini(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None)
    try:
        with path.open(encoding="utf-8-sig") as stream:
            parser.read_file(stream)
    except (OSError, UnicodeError, configparser.Error):
        raise BackupError("Cannot read INI configuration; check its path and syntax.") from None
    return parser


def relative_path(base: Path, value: str) -> Path:
    path = Path(value).expanduser()
    return (base / path).resolve() if not path.is_absolute() else path.resolve()


def load_settings(path: Path) -> Settings:
    parser = read_ini(path)
    try:
        databases = {
            name: relative_path(path.parent, value.strip())
            for name, value in parser.items("databases")
        }
        if not {"blog", "artalk"}.issubset(databases):
            raise BackupError("Both blog and artalk databases must be configured.")
        for name, database in databases.items():
            if not re.fullmatch(r"[a-z][a-z0-9_-]{0,39}", name):
                raise BackupError("Invalid database label in configuration.")
            if not database.is_file():
                raise BackupError(f"Database does not exist: {name}; no empty database was created.")
        keep = parser.getint("backup", "local_keep", fallback=6)
        timeout = parser.getint("backup", "snapshot_timeout_seconds", fallback=600)
        if keep < 1 or timeout < 1:
            raise BackupError("local_keep and snapshot_timeout_seconds must be positive.")
        prefix = parser.get("qiniu", "prefix", fallback="blog-databases").strip("/")
        if not re.fullmatch(r"[a-zA-Z0-9/_-]+", prefix) or ".." in prefix:
            raise BackupError("Invalid object prefix.")
        return Settings(
            databases=databases,
            output_dir=relative_path(path.parent, parser.get("backup", "output_dir", fallback="backups")),
            local_keep=keep,
            snapshot_timeout=timeout,
            bucket=parser.get("qiniu", "bucket").strip(),
            prefix=prefix,
            credentials_file=relative_path(path.parent, parser.get("qiniu", "credentials_file")),
        )
    except (ValueError, configparser.Error):
        raise BackupError("Invalid or missing backup configuration option.") from None


@contextmanager
def job_lock(directory: Path):
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    if os.name == "posix":
        directory.chmod(0o700)
    with (directory / ".backup.lock").open("a+b") as lock:
        lock.seek(0, 2)
        if lock.tell() == 0:
            lock.write(b"0")
            lock.flush()
        lock.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise BackupError("Another backup process is already running.") from None
        try:
            yield
        finally:
            lock.seek(0)
            if os.name == "nt":
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock, fcntl.LOCK_UN)


def snapshot_database(source: Path, target: Path, timeout: int) -> None:
    deadline = time.monotonic() + timeout

    def progress(_status, _remaining, _total):
        if time.monotonic() > deadline:
            raise BackupError("SQLite snapshot timed out; source database was left intact.")

    # mode=ro is deliberate: a typo must never silently create an empty database.
    with closing(sqlite3.connect(source.as_uri() + "?mode=ro", uri=True, timeout=5)) as reader:
        with closing(sqlite3.connect(target)) as writer:
            reader.backup(writer, pages=256, progress=progress, sleep=0.1)
            if writer.execute("PRAGMA integrity_check").fetchall() != [("ok",)]:
                raise BackupError("Snapshot integrity check failed; nothing will be uploaded.")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def create_archive(settings: Settings) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    archive = settings.output_dir / f"blog-backup-{stamp}-{uuid.uuid4().hex[:8]}.tar.gz"
    partial = archive.with_suffix(archive.suffix + ".partial")
    try:
        with tempfile.TemporaryDirectory(prefix="snapshot-", dir=settings.output_dir) as stage:
            stage = Path(stage)
            manifest = {"format": 1, "created_at_utc": datetime.now(timezone.utc).isoformat(), "databases": {}}
            for name, source in settings.databases.items():
                snapshot = stage / f"{name}.db"
                snapshot_database(source, snapshot, settings.snapshot_timeout)
                manifest["databases"][name] = {
                    "file": snapshot.name,
                    "sha256": sha256_file(snapshot),
                    "bytes": snapshot.stat().st_size,
                }
            (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            # Explicit whitelist: never archive the source directory, configuration, or credentials.
            with tarfile.open(partial, "w:gz") as bundle:
                for name in [f"{label}.db" for label in settings.databases] + ["manifest.json"]:
                    bundle.add(stage / name, arcname=name, recursive=False)
            partial.replace(archive)
        return archive
    finally:
        partial.unlink(missing_ok=True)


def qiniu_client(settings: Settings):
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{1,61}[a-z0-9]", settings.bucket):
        raise BackupError("Set a valid private Qiniu bucket in backup.ini first.")
    if os.name == "posix" and settings.credentials_file.stat().st_mode & 0o077:
        raise BackupError("Credentials file permissions are too broad; set chmod 600 first.")
    credentials = read_ini(settings.credentials_file)
    try:
        access_key = credentials.get("qiniu", "access_key").strip()
        secret_key = credentials.get("qiniu", "secret_key").strip()
        if not access_key or not secret_key:
            raise BackupError("Qiniu credentials are empty.")
    except configparser.Error:
        raise BackupError("Credentials file must contain [qiniu] access_key and secret_key.") from None
    import qiniu
    logging.getLogger("qiniu").setLevel(logging.CRITICAL)
    # The SDK defaults to HTTP for uploads. Explicitly require HTTPS for every service.
    zone = qiniu.Zone(scheme="https")
    qiniu.set_default(
        default_zone=zone, default_uc_host="https://uc.qiniuapi.com",
        default_rs_host="https://rs.qiniu.com", default_rsf_host="https://rsf.qbox.me",
        default_api_host="https://api.qiniuapi.com", connection_timeout=30,
    )
    auth = qiniu.Auth(access_key, secret_key)
    manager = qiniu.BucketManager(auth, zone=zone, preferred_scheme="https")
    info, response = manager.bucket_info(settings.bucket)
    if response.status_code != 200 or not isinstance(info, dict) or info.get("private") != 1:
        raise BackupError("Cannot verify that the destination bucket is private; upload refused.")
    # In SDK 7.16, a customized default Zone is used as-is by the uploader.
    # A scheme-only Zone has no UP endpoints. Query and pass actual regions
    # explicitly while retaining HTTPS for form AND multipart uploads.
    from qiniu.http.endpoint import Endpoint
    from qiniu.http.regions_provider import get_default_regions_provider
    manager.regions = list(get_default_regions_provider(
        access_key=access_key, bucket_name=settings.bucket, preferred_scheme="https",
        query_endpoints_provider=[Endpoint.from_host("https://uc.qiniuapi.com")],
    ))
    if not manager.regions:
        raise BackupError("No upload region is available for the private backup bucket.")
    return qiniu, auth, manager


def upload_archive(settings: Settings, archive: Path, client) -> str:
    qiniu, auth, manager = client
    key = f"{settings.prefix}/{archive.name[12:16]}/{archive.name}"
    expected_hash = qiniu.etag(str(archive))
    expected_size = archive.stat().st_size
    for attempt in range(1, 4):
        try:
            token = auth.upload_token(settings.bucket, key, 3600, {"insertOnly": 1})
            result, response = qiniu.put_file(
                token, key, str(archive), check_crc=True, version="v2", bucket_name=settings.bucket,
                regions=manager.regions,
            )
            # A lost response may mean the upload succeeded. Stat also makes retries idempotent.
            remote, stat_response = manager.stat(settings.bucket, key)
            if (stat_response.status_code == 200 and isinstance(remote, dict)
                    and remote.get("hash") == expected_hash and remote.get("fsize") == expected_size):
                return key
        except Exception:
            # SDK exceptions may include authorization headers; never log their messages/repr.
            pass
        LOG.warning("Upload/verification attempt %d of 3 failed.", attempt)
        if attempt < 3:
            time.sleep(2 ** attempt)
    raise BackupError("Upload could not be verified; the local archive was retained for retry.")


def cleanup_uploaded(settings: Settings) -> None:
    # Only prune archives with our own name AND an upload-success marker.
    uploaded = sorted(
        path for path in settings.output_dir.iterdir()
        if ARCHIVE_PATTERN.fullmatch(path.name) and path.with_suffix(path.suffix + ".uploaded").is_file()
    )
    for archive in uploaded[:-settings.local_keep]:
        archive.unlink()
        archive.with_suffix(archive.suffix + ".uploaded").unlink(missing_ok=True)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path(__file__).with_name("backup.ini"))
    parser.add_argument("--snapshot-only", action="store_true", help="Create and verify local archive; no cloud access.")
    parser.add_argument("--check", action="store_true", help="Validate sources, credentials and private bucket; no snapshot/upload.")
    parser.add_argument("--retry", type=Path, help="Retry an existing archive from output_dir without making a new snapshot.")
    args = parser.parse_args(argv)
    if sum(bool(flag) for flag in (args.snapshot_only, args.check, args.retry)) > 1:
        parser.error("--snapshot-only, --check and --retry are mutually exclusive")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    os.umask(0o077)
    try:
        settings = load_settings(args.config.resolve())
        with job_lock(settings.output_dir):
            client = None if args.snapshot_only else qiniu_client(settings)
            if args.check:
                LOG.info("Configuration, both database paths and private bucket verified.")
                return 0
            if args.retry:
                archive = args.retry.resolve()
                if (archive.parent != settings.output_dir or not ARCHIVE_PATTERN.fullmatch(archive.name)
                        or not archive.is_file() or args.retry.is_symlink()):
                    raise BackupError("Retry requires an existing backup archive directly inside output_dir.")
            else:
                archive = create_archive(settings)
            LOG.info("Verified local archive: %s (sha256=%s)", archive.name, sha256_file(archive))
            if args.snapshot_only:
                return 0
            key = upload_archive(settings, archive, client)
            archive.with_suffix(archive.suffix + ".uploaded").touch(mode=0o600)
            cleanup_uploaded(settings)
            LOG.info("Backup uploaded and verified: %s/%s", settings.bucket, key)
        return 0
    except BackupError as error:
        LOG.error("%s", error)
    except Exception as error:
        LOG.error("Backup failed (%s). No SDK error details are printed to protect credentials.", type(error).__name__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
