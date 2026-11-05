#!/usr/bin/env bash
# 生成中文字体子集 topology-editor/resources/fonts/NotoSansSC-subset.otf
# 源码新增了 GB2312 一级字以外的汉字时重新运行一次。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/.cache/fonts"
OUT="$ROOT/topology-editor/resources/fonts/NotoSansSC-subset.otf"
FONT_URL="${FONT_URL:-https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf}"
PY_IMAGE="${PY_IMAGE:-docker.1ms.run/library/python:3.12-slim}"

mkdir -p "$CACHE" "$(dirname "$OUT")"
if [[ ! -s "$CACHE/NotoSansSC-Regular.otf" ]]; then
    echo ">> 下载 Noto Sans SC ..."
    curl -fL --retry 3 -o "$CACHE/NotoSansSC-Regular.otf" "$FONT_URL"
fi

python3 "$ROOT/scripts/font_chars.py" "$ROOT/topology-editor/src" "$CACHE/chars.txt"

echo ">> 子集化 ..."
docker run --rm --network host \
    -e HTTP_PROXY -e HTTPS_PROXY -e http_proxy -e https_proxy \
    -v "$CACHE:/work" "$PY_IMAGE" sh -c '
        pip install -q --root-user-action=ignore fonttools &&
        pyftsubset /work/NotoSansSC-Regular.otf \
            --text-file=/work/chars.txt \
            --layout-features="kern,liga,vert" \
            --no-hinting --desubroutinize \
            --name-IDs="*" \
            --output-file=/work/subset.otf'
cp "$CACHE/subset.otf" "$OUT"
ls -lh "$OUT"
