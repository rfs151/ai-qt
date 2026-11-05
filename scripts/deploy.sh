#!/usr/bin/env bash
# 在本机构建镜像，传到 VPS 并用 docker compose 启动。
# VPS 上不需要 Qt/emsdk，也不需要访问 Docker Hub（镜像直接通过 ssh 传过去）。
#
#   ./scripts/deploy.sh user@1.2.3.4                 # 部署到 ~/qt-showcase
#   ./scripts/deploy.sh user@1.2.3.4 /opt/qt-showcase
#   SKIP_BUILD=1 ./scripts/deploy.sh user@1.2.3.4    # 不重新构建，直接传现有镜像
#
# .env 中设置了 DOMAIN 时自动启用 HTTPS（Caddy）。
set -euo pipefail

HOST="${1:?用法: deploy.sh user@host [远程目录]}"
REMOTE_DIR="${2:-qt-showcase}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

[[ -f .env ]] || cp .env.example .env
set -a && source .env && set +a
IMAGE="${IMAGE:-qt-showcase:latest}"

if [[ -z "${SKIP_BUILD:-}" ]]; then
    echo ">> 构建镜像 $IMAGE"
    docker compose build
fi

echo ">> 上传镜像到 $HOST（压缩后约 10MB）"
docker save "$IMAGE" | gzip | ssh "$HOST" 'gunzip | docker load'

echo ">> 同步部署文件"
ssh "$HOST" "mkdir -p '$REMOTE_DIR/docker'"
scp docker-compose.yml docker-compose.https.yml "$HOST:$REMOTE_DIR/"
scp docker/Caddyfile "$HOST:$REMOTE_DIR/docker/"
# 远程 .env：VPS 能直接访问 Docker Hub 时不需要镜像源
grep -vE '^(REGISTRY|AQT_BASE|PIP_INDEX_URL)=' .env | ssh "$HOST" "cat > '$REMOTE_DIR/.env'"

COMPOSE_FILES="-f docker-compose.yml"
if [[ -n "${DOMAIN:-}" ]]; then
    COMPOSE_FILES="$COMPOSE_FILES -f docker-compose.https.yml"
    echo ">> 启用 HTTPS：$DOMAIN"
fi

echo ">> 启动服务"
ssh "$HOST" "cd '$REMOTE_DIR' && docker compose $COMPOSE_FILES up -d --no-build --remove-orphans && docker compose $COMPOSE_FILES ps"

if [[ -n "${DOMAIN:-}" ]]; then
    echo ">> 完成：https://$DOMAIN"
else
    echo ">> 完成：http://${HOST#*@}:${HTTP_PORT:-80}"
fi
