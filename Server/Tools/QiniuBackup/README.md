# 主站与评论数据库 → 七牛云私有备份

在线备份 BlogServer 与 Artalk 的 SQLite 数据库，无需停服，不覆盖源库。每个备份包包含 `blog.db`、`artalk.db`、`manifest.json`，没有配置文件、SSL 私钥或七牛密钥。两个数据库分别获得一致快照，不承诺跨库同一事务时刻。

## 当前部署（2026-11-12）

已部署到现有服务器并完成真实上传与 ETag/大小校验。目的空间为 `feiqi3-db-backups`，华东 `z0`，私有；对象前缀为 `blog-databases/年份/`。`blog-db-backup.timer` 已启用并处于等待状态，每月 1 日和 16 日 03:00（香港时间）执行。首次通过 systemd 手动运行结果为 success / exit 0。真实配置和密钥仅保留在服务器，不在本仓库中。

## 配置

### 1. 配置文件放在哪里

下面的命令均在 **Linux 服务器** 上执行。当前已经配置并运行，无需重新创建或覆盖现有配置；需要调整时编辑真实 `backup.ini` 即可。

- 程序：`/root/BlogServer/Server/Tools/QiniuBackup/backup.py`
- 实际配置：`/root/BlogServer/Server/Tools/QiniuBackup/backup.ini`（不提交 Git）
- 可提交的配置模板：同目录 `backup.example.ini`
- 密钥文件：由 `credentials_file` 指向，独立于主配置；下面提供两种方式。

脚本默认读取自身目录中的 `backup.ini`；也可显式指定：`python3 backup.py --config /绝对路径/backup.ini`。**所有相对路径都以该 INI 所在目录为基准**，不是终端当前目录，也不是 Python 脚本所在目录。例如默认主站路径 `../../resources/database/blog.db` 从本工具目录解析后，正好是 `/root/BlogServer/Server/resources/database/blog.db`。

### 2. 首次准备环境

建议新建一个专用的**私有**七牛 Kodo Bucket，与公开图片资源分开。不要绑定公开 CDN、不要改为公开读。脚本会通过七牛接口检查私有属性，不能确认就停止上传。

在服务器 `/root/BlogServer/Server/Tools/QiniuBackup/` 中：

```bash
cd /root/BlogServer/Server/Tools/QiniuBackup
umask 077
python3 -m pip install -r requirements.txt
cp -n backup.example.ini backup.ini  # 已有配置时保留原文件
chmod 600 backup.ini
# 用编辑器打开 backup.ini，按下方完整示例填写。
```

当前服务器使用 Python 3.10、qiniu 7.16.0；建议新环境使用 Python 3.10 或以上。若系统 Python 不允许 pip 安装，使用虚拟环境，并同步调整 service 中的 Python 解释器路径。

### 3. backup.ini 完整示例与字段说明

下面是当前部署方案的完整配置，**不含任何密钥**。新环境至少要修改 `bucket`、确认 `blog` / `artalk` 的路径，并设置正确的 `credentials_file`。

```ini
[backup]
output_dir = backups
local_keep = 6
snapshot_timeout_seconds = 600

[databases]
blog = ../../resources/database/blog.db
artalk = /opt/artalk/data/artalk.db

[qiniu]
bucket = feiqi3-db-backups
prefix = blog-databases
credentials_file = ../SslCertFIleDnsChallenge_TxDnsPos_to_Qiniu/config.ini
```

| 配置项 | 含义与填写规则 |
| --- | --- |
| `backup.output_dir` | 本机归档目录；默认 `backups`，相对 INI 目录解析。运行用户必须可写，Linux 下目录权限设为 700。 |
| `backup.local_keep` | 保留最近多少份**已确认上传**的本地归档，正整数，默认 6；失败包不自动删除。此项不控制云端保留期。 |
| `backup.snapshot_timeout_seconds` | 每个数据库通过 SQLite backup API 制作快照的超时秒数，正整数，默认 600。定时服务另有 30 分钟整体超时。 |
| `databases.blog` | 主站 SQLite 文件路径，必须指向现有文件，不能填目录；必填。 |
| `databases.artalk` | Artalk 评论 SQLite 文件路径，需与 Artalk 自身 `artalk.yml` 的 `db.file` 一致；必填。 |
| `qiniu.bucket` | 已创建且有权访问的私有 Kodo 空间名，不是域名或 URL；必填。脚本不自动创建空间。 |
| `qiniu.prefix` | 云端对象前缀，默认 `blog-databases`；允许字母、数字、斜杠、下划线和短横线。对象示例：`blog-databases/2026/blog-backup-时间戳-随机值.tar.gz`。 |
| `qiniu.credentials_file` | 密钥 INI 文件路径，必填；内容必须包含 `[qiniu]`、`access_key`、`secret_key`。相对路径仍以主配置所在目录为基准。 |

Bucket 区域由 SDK 查询，无需填写 region 或上传域名；HTTPS 已由程序强制设置。云端保留期不在此 INI 中控制，目前不自动删除历史备份。

默认路径已按当前服务器核实：

- 主站：`/root/BlogServer/Server/resources/database/blog.db`
- 评论：`/opt/artalk/data/artalk.db`

### 4. 如何配置密钥

**方式 A：复用服务器现有配置（当前使用）。**

`credentials_file` 默认读取服务器已有续签工具的 `config.ini`，只取其中 `[qiniu]` 的 `access_key` / `secret_key`，不复制、不输出它们。该文件在服务器上必须限制为仅所有者可读写：

```bash
chmod 600 ../SslCertFIleDnsChallenge_TxDnsPos_to_Qiniu/config.ini
```

**方式 B：使用单独的备份密钥文件。**

```bash
cd /root/BlogServer/Server/Tools/QiniuBackup
umask 077
cp -n credentials.example.ini credentials.ini
chmod 600 credentials.ini
# 用服务器上的编辑器填写 AK/SK；不要通过聊天或带密钥的命令参数传入。
```

`credentials.ini` 的格式如下，值仅为占位符，必须自行替换：

```ini
[qiniu]
access_key = REPLACE_WITH_YOUR_ACCESS_KEY
secret_key = REPLACE_WITH_YOUR_SECRET_KEY
```

再将 `backup.ini` 中这一项改为：

```ini
credentials_file = credentials.ini
```

主配置里不填写 AK/SK。推荐使用最小权限凭据，需允许目的 Bucket 的信息读取、上传和对象 stat 校验。运行服务的用户必须能读取密钥文件；Linux 下只要密钥文件允许组或其他用户访问，脚本就会拒绝运行，先修正为 `chmod 600`。

**真实 backup.ini、credentials.ini、备份包均已被本目录 .gitignore 排除；只提交 example 文件。** 若把真实密钥文件放到其他位置，需另外确保该位置不纳入版本管理。上传代码时使用明确文件清单，不能把真实配置一起 scp/打包。

### 5. 修改后怎样生效

每次运行都会重新读配置，修改 `backup.ini` 或密钥后，不需要重启主站、Artalk，也不需要重启 timer。先执行 `python3 backup.py --check`，成功后可手动执行 `systemctl start blog-db-backup.service` 验证一次真实上传。只有修改服务的解释器、安装路径或配置文件路径时，才需要调整 systemd service 并 `systemctl daemon-reload`。

## 手动验证

```bash
# 完全离线：制作一致快照、integrity_check、压缩和 SHA-256 清单；不读取七牛密钥。
python3 backup.py --snapshot-only
# 校验配置、源文件、凭据权限与 Bucket 私有属性；不上传。
python3 backup.py --check
# 完整备份上传；成功后再次从七牛 stat 验证 ETag 和大小。
python3 backup.py
```

所有相对配置路径以 INI 所在目录为基准，不依赖 cron/systemd 的当前目录。日志只包含状态、归档名称与校验值，不打印 SDK 响应或异常详情；退出码 0 成功、1 失败。

备份通过 SQLite backup API 获取在线 WAL 数据，然后对每份快照执行 integrity_check。只读打开源库，路径错误不会创建空数据库。不直接复制活动中的 db，不删除 WAL/SHM。全程只有两份快照及其 JSON 清单进入压缩包，使用 HTTPS 上传和 insertOnly，云端按 UTC 时间戳/随机后缀生成新对象，不覆盖旧备份。

本地备份放在权限 700 的 `backups/` 中，保留最近 6 份已确认上传的归档；未上传或失败的归档不自动删除，便于人工重试。云端不自动删除历史备份，可在确认保留期后单独配置生命周期。应定期检查失败状态及本地磁盘空间。

```bash
# 手动重试失败包，文件必须位于配置的 output_dir 中。
python3 backup.py --retry backups/blog-backup-实际文件名.tar.gz
```

## 半月定时任务

提供 systemd service + timer 和安装脚本，使用操作系统定时器，不依赖打开终端。每月 **1 日、16 日凌晨 03:00，Asia/Hong_Kong** 执行；这是每月两次，间隔随月份天数略有变化。`Persistent=true` 会在关机错过计划后开机补执行一次。进程锁防止手动与定时任务并发。

确认手动完整上传成功后：

```bash
bash install_timer.sh
systemctl list-timers --all blog-db-backup.timer
journalctl -u blog-db-backup.service -n 50 --no-pager
# 手动通过 systemd 执行一次（会真实上传）。
systemctl start blog-db-backup.service
# 关闭定时任务。
systemctl disable --now blog-db-backup.timer
```

默认使用现有服务器 `/usr/bin/python3`（已安装 qiniu 7.16.0）。若使用虚拟环境或更改安装目录，先调整 service 的 WorkingDirectory/ExecStart。服务超时上限 30 分钟；失败会在 systemd 中显示 failed。不会重启 BlogServer 或 Artalk。

### 如何修改执行时间

时间由 `blog-db-backup.timer` 的 `OnCalendar` 控制，**不在 backup.ini 里**。在服务器上执行 `systemctl edit blog-db-backup.timer`，例如改为每天凌晨 03:00，填写：

```ini
[Timer]
OnCalendar=
OnCalendar=*-*-* 03:00:00 Asia/Hong_Kong
```

空的 `OnCalendar=` 用于清除原计划，不能省略，否则可能同时保留两条计划。保存后执行：

```bash
systemctl daemon-reload
systemctl restart blog-db-backup.timer
systemctl list-timers --all blog-db-backup.timer
```

原有每月 1 日、16 日的表达式是 `*-*-01,16 03:00:00 Asia/Hong_Kong`。这里只是修改方法，当前线上仍保持半月计划。

## 常见配置问题

| 表现 | 检查方法 |
| --- | --- |
| 找不到数据库 | 核实源文件真实路径及运行用户的访问权限；特别注意相对路径以 INI 目录为准。脚本不会自动创建空库。 |
| 配置无法读取或缺少字段 | 对照完整 INI 示例；文件使用 UTF-8，字段写在正确的段中；不要把配置值写成带引号的字符串。 |
| 密钥权限过宽 | 对 `credentials_file` 指向的实际文件执行 `chmod 600`，确认服务用户为该文件所有者。 |
| 无法确认 Bucket 为私有 | 核实空间名、私有权限、AK/SK 所属账号以及读取空间信息的权限。为保护数据库，检查失败不会继续上传。 |
| 上传或校验失败 | 看 `journalctl -u blog-db-backup.service -n 50 --no-pager`；核实网络和上传/stat 权限，失败包会留在 output_dir，可用 `--retry` 重试。不要通过开启 SDK 调试日志打印密钥或上传凭证。 |
| 定时器没有下一次执行时间 | 检查 `systemctl is-enabled blog-db-backup.timer`、`systemctl is-active blog-db-backup.timer`，修改计划后重新加载并重启 timer。 |

## 恢复验证

从私有 Bucket 下载指定 `.tar.gz`，在隔离目录解压，按 manifest.json 核对两份文件的 SHA-256，并执行：

```bash
sqlite3 -readonly blog.db 'PRAGMA integrity_check;'
sqlite3 -readonly artalk.db 'PRAGMA integrity_check;'
```

两个结果均应为 ok。恢复生产是独立操作：先停止对应服务、备份当前库，再按服务自身方式恢复；本脚本不自动恢复或覆盖任何生产数据库。

离线测试：`python3 -m unittest discover -s . -p 'test_backup.py' -v`，覆盖 WAL 恢复、缺失/损坏数据库、重复运行、私有空间限制、上传校验、重试、日志脱敏与本地保留规则，不使用真实云密钥。

接口依据：[七牛 Python SDK](https://developer.qiniu.com/kodo/1242/python)，依赖版本与当前服务器保持为 7.16.0。
