#!/usr/bin/env bash
# Run on the Linux server after completing backup.ini and a successful manual backup.
set -euo pipefail
umask 077
TOOL_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [ "$(id -u)" -ne 0 ]; then
  echo 'Run this installer as root.' >&2
  exit 1
fi
if [ "$TOOL_DIR" != /root/BlogServer/Server/Tools/QiniuBackup ]; then
  echo 'Adjust the service paths before installing from a different directory.' >&2
  exit 1
fi
/usr/bin/python3 "$TOOL_DIR/backup.py" --config "$TOOL_DIR/backup.ini" --check
install -m 644 "$TOOL_DIR/blog-db-backup.service" /etc/systemd/system/blog-db-backup.service
install -m 644 "$TOOL_DIR/blog-db-backup.timer" /etc/systemd/system/blog-db-backup.timer
systemd-analyze verify /etc/systemd/system/blog-db-backup.service /etc/systemd/system/blog-db-backup.timer
systemctl daemon-reload
systemctl enable --now blog-db-backup.timer
systemctl list-timers --all blog-db-backup.timer --no-pager
