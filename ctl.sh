#!/usr/bin/env bash
# ai-qt 网络拓扑编辑器（Qt WebAssembly，nginx） —— 本机启停 / 查看状态（部署方式：纯 Docker Compose，容器 restart: unless-stopped，随 Docker 开机自启）
#
#   ./ctl.sh start            启动全部容器（不重新构建；改了代码用 docker compose up -d --build）
#   ./ctl.sh stop             停止全部容器（保留容器和数据）
#   ./ctl.sh restart [服务]   重启全部或指定服务
#   ./ctl.sh status           容器状态 + 健康检查
#   ./ctl.sh logs [服务]      跟踪日志
#
# 本机：http://127.0.0.1:8080/healthz    公网（国内 VPS frp 转发）：http://47.117.29.74:8080/
set -euo pipefail
cd "$(dirname "$0")"
DC=(docker compose)
HEALTH=http://127.0.0.1:8080/healthz
PUBLIC=http://47.117.29.74:8080/

health() {
  local code
  code=$(curl -s --noproxy '*' -m 5 -o /dev/null -w '%{http_code}' "$HEALTH" || true)
  echo "健康检查 $HEALTH -> ${code:-无响应}"
  echo "公网地址 $PUBLIC"
}

case ${1:-status} in
  start)   "${DC[@]}" up -d ;;
  stop)    "${DC[@]}" stop ;;
  restart) shift; "${DC[@]}" restart "$@" ;;
  status)  "${DC[@]}" ps -a --format 'table {{.Service}}\t{{.Status}}\t{{.Ports}}'; health ;;
  logs)    shift; "${DC[@]}" logs -f --tail 100 "$@" ;;
  *)       sed -n '2,9p' "$0"; exit 1 ;;
esac
