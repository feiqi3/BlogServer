# GoatCounter 凭据管理工具

一键修改本机 GoatCounter 的登录凭据。**部署在哪就改哪**——Windows、WSL、远程生产服务器通用同一套脚本。

## 文件

| 文件 | 说明 |
|---|---|
| `gc_update_credentials.py` | 主脚本（Python 3，仅标准库） |
| `gc_credentials.cfg` | 凭据配置（明文密码，**已 gitignore，禁止提交**） |

## 用法

```bash
# 1. 查看 GC 当前状态（只读，不改任何东西）
python3 gc_update_credentials.py --status

# 2. 改登录凭据：先编辑 gc_credentials.cfg 里的 GC_EMAIL / GC_PASSWORD，然后
python3 gc_update_credentials.py

# 3. 连站点 vhost 一起改（例如 WSL IP 变了：改 cfg 的 GC_VHOST 后）
python3 gc_update_credentials.py --with-vhost
```

凭据也可以通过环境变量传入（不落盘）：

```bash
GC_EMAIL=admin@feiqi3.cn GC_PASSWORD='xxx' python3 gc_update_credentials.py
```

## 工作方式

1. **自动探测本机 GC 位置**（找到第一个存在的）：

   | 环境 | 二进制 | 数据库 |
   |---|---|---|
   | 生产（远程） | `/opt/goatcounter/goatcounter` | `/opt/goatcounter/goatcounter.db` |
   | WSL dev | `~/goatcounter/goatcounter` | `~/goatcounter/goatcounter.db` |
   | 脚本同目录 | `./goatcounter(.exe)` | `./goatcounter.db` |

2. 改密码走 `goatcounter db update user`（bcrypt 哈希入库，**即时生效，无需重启 GC**）
3. 定位现有用户/站点用 sqlite 只读直查（GC CLI 的 `-find` 只支持精确匹配）

## 各环境凭据体系（互不相干）

| 系统 | 登录名 | 管理位置 |
|---|---|---|
| GoatCounter 面板 | cfg 中的 GC_EMAIL | 本工具 |
| BlogServer 后台 | server.cfg AdminUser/AdminPassword | `Server/resources/config/server.cfg` |
| Artalk | Artalk 自己的后台 | `/opt/artalk/artalk.yml` |

## 注意事项

- **cfg 是明文密码**：BlogServer 的 `.gitignore` 已包含 `Server/Tools/GoatCounter/gc_credentials*.cfg`，不要删除这条规则
- WSL 的 IP（当前 172.26.33.224）重启后可能变化：变了需要 ① `--with-vhost` 更新 GC 站点 ② 改 dev 的 `server.cfg` GoatCounterUrl
- 安装时的初始密码（见 `.opencode/scripts/gc-install-remote.sh`）装完就用本工具换掉
