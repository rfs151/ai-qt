#!/usr/bin/env bash
# 本地开发用：在工具链容器里增量编译 WASM 版，产物放在 build-wasm/，
# 然后起一个本地 HTTP 服务预览（不能直接双击 html 打开）。
#
#   ./scripts/build-wasm.sh          # 编译
#   ./scripts/build-wasm.sh serve    # 编译并在 http://localhost:8000 预览
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
[[ -f .env ]] && set -a && source .env && set +a

QT_VERSION="${QT_VERSION:-6.8.3}"
TOOLCHAIN_IMAGE="qt-wasm-toolchain:${QT_VERSION}"

if ! docker image inspect "$TOOLCHAIN_IMAGE" >/dev/null 2>&1; then
    echo ">> 构建工具链镜像 $TOOLCHAIN_IMAGE（首次约需 10 分钟）"
    docker build --network host -f docker/Dockerfile --target toolchain -t "$TOOLCHAIN_IMAGE" \
        --build-arg REGISTRY="${REGISTRY:-docker.io}" \
        --build-arg AQT_BASE="${AQT_BASE:-https://download.qt.io}" \
        --build-arg PIP_INDEX_URL="${PIP_INDEX_URL:-https://pypi.org/simple}" \
        --build-arg QT_VERSION="$QT_VERSION" \
        --build-arg HTTP_PROXY --build-arg HTTPS_PROXY --build-arg NO_PROXY \
        .
fi

echo ">> 编译"
docker run --rm -v "$ROOT:/work" -w /work "$TOOLCHAIN_IMAGE" bash -c '
    set -e
    [[ -f build-wasm/build.ninja ]] || \
        $QT_WASM/bin/qt-cmake -S topology-editor -B build-wasm -G Ninja \
            -DCMAKE_BUILD_TYPE=MinSizeRel -DQT_HOST_PATH=$QT_HOST
    cmake --build build-wasm --parallel
'

# 组装成和线上相同的目录结构
rm -rf dist && mkdir -p dist/demo/topology
cp -r web/. dist/
cp build-wasm/topology.js build-wasm/topology.wasm build-wasm/qtloader.js dist/demo/topology/
# 加载页用 wasm 原始大小计算下载进度
sed -i "s/__WASM_SIZE__/$(stat -c%s dist/demo/topology/topology.wasm)/" dist/demo/topology/index.html
ls -lh dist/demo/topology/

if [[ "${1:-}" == "serve" ]]; then
    echo ">> 预览：http://localhost:8000"
    cd dist && python3 -m http.server 8000
fi
