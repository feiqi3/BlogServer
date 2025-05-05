#!/usr/bin/env bash
set -euo pipefail

# —————————————————————————
# 配置区
# —————————————————————————
# Webroot 验证目录
WEBROOT="$(cd "$(dirname "$0")")/../resources/web/other/certbot"
# 证书存放目录
CERTDIR="$(cd "$(dirname "$0")")/../resources/SSL"
# 域名列表（用空格分隔）
DOMAINS=(feiqi3.cn www.feiqi3.cn)
# 联系邮箱
EMAIL="zjgtzn@outlook.com"

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