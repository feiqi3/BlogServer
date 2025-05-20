#!/usr/bin/env bash
set -euo pipefail

# —————————————————————————
# 检查参数
# —————————————————————————
if [ $# -ne 2 ]; then
  echo "用法: $0 <服务器资源路径，例如 BlogServer\Server\resources> $1 <服务器二进制路径，例如 BlogServer\build\release\server>"
  exit 1
fi

# —————————————————————————
# 配置区
# —————————————————————————
# Webroot 验证目录
WEBROOT="$1/temp/certbot"
# 证书存放目录
CERTDIR="$1/SSL"
# 域名列表（用空格分隔）
DOMAINS=(feiqi3.cn www.feiqi3.cn)
# 联系邮箱
EMAIL="zjgtzn@outlook.com"

SERVER_EXEC="$2"

SHUTDOWN_URL="http://localhost/shutdown"
# 构建 -d 参数
DOMAIN_FLAGS=()
for d in "${DOMAINS[@]}"; do
  DOMAIN_FLAGS+=("-d" "$d")
done



# —————————————————————————
# 执行更新
# —————————————————————————
certbot certonly \
  --webroot \
  --webroot-path "$WEBROOT" \
  "${DOMAIN_FLAGS[@]}" \
  --email "$EMAIL" \
  --agree-tos \
  --non-interactive \
  --config-dir "$CERTDIR/config" \
  --work-dir "$CERTDIR/work" \
  --logs-dir "$CERTDIR/logs" \
  --force-renewal

echo "[$(date '+%F %T')] 证书已更新，包含域名：${DOMAINS[*]}"
echo "证书路径：$CERTDIR/config/live/${DOMAINS[0]}/"
if curl --silent --show-error --fail "$SHUTDOWN_URL"; then
  echo "[$(date '+%F %T')] 成功发送关机请求"
else
  echo "[$(date '+%F %T')] 发送关机请求失败"
fi

echo "[$(date '+%F %T')] 等待 30 秒服务器关机"
sleep 30

cp "$CERTDIR/config/live/${DOMAINS[0]}/fullchain.pem" "$CERTDIR/cert.pem"
cp "$CERTDIR/config/live/${DOMAINS[0]}/privkey.pem"   "$CERTDIR/private.pem"

# —————————————————————————
# 等待 一段时间后 重新启动服务器
# —————————————————————————
echo "[$(date '+%F %T')] 等待 90 秒后启动：$SERVER_EXEC"
sleep 90

echo "[$(date '+%F %T')] 启动服务器：$SERVER_EXEC"
nohup "$SERVER_EXEC"
disown

echo "[$(date '+%F %T')] 服务器已启动"