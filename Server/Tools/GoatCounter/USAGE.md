# GoatCounter 访问统计 -- 使用指南

## 访问后台

1. 浏览器打开 **https://feiqi3.cn:23367**
2. 跳转到登录页，输入凭据登录：
   - 邮箱：`admin@feiqi3.cn`（初始密码 `admin12345`，**装完立即改**）
   - 改密码：`python3 Server/Tools/GoatCounter/gc_update_credentials.py`
3. 登录后即看到 Dashboard

## Dashboard 能看什么

| 面板 | 内容 |
|---|---|
| Pages | 每篇文章的访问量（按路径排序） |
| Total pages | 总访问量趋势线（按日/周/月） |
| Top refs | 来源（从哪些链接点过来的） |
| Browsers | 浏览器分布（Chrome/Safari/Firefox…） |
| Systems | 操作系统分布 |
| Locations | 访客地域 |
| Languages | 语言 |
| Sizes | 屏幕尺寸 |

顶部可切换时间范围（今天/本周/本月/自定义）。

## 统计范围

- **仅文章详情页**（`/post/{id}`）被统计
- 首页、分类、归档、后台**不统计**
- 原理：article.html head 里嵌了 GC 脚本（`data-goatcounter`），页面加载时浏览器发 POST /count 上报
- 在 server.cfg 里配 `GoatCounterUrl`（空=关闭统计，有值=开启）

## 架构

```
访客浏览器 ──> BlogServer (80/443)     返回 article.html（含 GC 脚本）
         ──> GoatCounter (23367/TLS)  上报访问数据，存 GC 自己的 sqlite
```

- GC 独立进程，systemd 管理（`systemctl status/restart goatcounter`）
- TLS 复用主域 LE 证书（leaf+key 合并为 combined.pem）
- 数据库：`/opt/goatcounter/goatcounter.db`（已纳入备份脚本）
- 不经过 BlogServer，不影响博客性能

## 程序化查询（API）

```bash
# 先在 GC 面板 Settings -> API keys 创建 token（perm: count）
TOKEN=你的token
curl -sk -H "Authorization: Bearer $TOKEN" \
  https://feiqi3.cn:23367/api/v0/stats/total?start=2026-08-23
```

## 运维命令

```bash
# 状态
systemctl status goatcounter

# 重启
systemctl restart goatcounter

# 查看日志
journalctl -u goatcounter -n 50 --no-pager

# 改登录凭据
python3 /root/BlogServer/Server/Tools/GoatCounter/gc_update_credentials.py --status
python3 /root/BlogServer/Server/Tools/GoatCounter/gc_update_credentials.py

# 证书续签后自动同步（已集成进 genSSL.sh，无需手动操作）
```

## 文件位置

| 路径 | 说明 |
|---|---|
| `/opt/goatcounter/goatcounter` | 二进制 |
| `/opt/goatcounter/goatcounter.db` | 数据库（已备份） |
| `/opt/goatcounter/ssl/combined.pem` | TLS 证书（leaf+key 合并） |
| `/etc/systemd/system/goatcounter.service` | systemd unit |
| `Server/Tools/GoatCounter/` | 凭据管理工具（py + cfg） |
