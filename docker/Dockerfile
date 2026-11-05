# syntax=docker/dockerfile:1
# 多阶段构建：toolchain(emsdk + Qt wasm) -> build(编译) -> runtime(nginx 静态站点)
#
# 国内网络可以换镜像源，例如：
#   --build-arg REGISTRY=docker.1ms.run
#   --build-arg AQT_BASE=https://mirrors.ustc.edu.cn/qtproject
#   --build-arg PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple

ARG REGISTRY=docker.io
# emsdk 版本必须与 Qt 版本严格对应：Qt 6.8 -> emsdk 3.1.56
ARG EMSDK_VERSION=3.1.56

# ------------------------------------------------------------------ toolchain
FROM ${REGISTRY}/emscripten/emsdk:${EMSDK_VERSION} AS toolchain
ARG QT_VERSION=6.8.3
ARG AQT_BASE=https://download.qt.io
ARG PIP_INDEX_URL=https://pypi.org/simple

RUN apt-get update \
 && apt-get install -y --no-install-recommends ninja-build python3-pip \
 && rm -rf /var/lib/apt/lists/*

# 宿主 Qt(gcc_64) 只用于提供 moc/rcc/uic 等构建工具；wasm_singlethread 是目标平台库
RUN python3 -m pip install --no-cache-dir -i "${PIP_INDEX_URL}" aqtinstall \
 && aqt install-qt -b "${AQT_BASE}" -O /opt/Qt linux desktop "${QT_VERSION}" linux_gcc_64 \
 && aqt install-qt -b "${AQT_BASE}" -O /opt/Qt all_os wasm "${QT_VERSION}" wasm_singlethread \
 && rm -rf /opt/Qt/${QT_VERSION}/*/doc /opt/Qt/${QT_VERSION}/*/examples aqtinstall.log

ENV QT_ROOT=/opt/Qt/${QT_VERSION} \
    QT_WASM=/opt/Qt/${QT_VERSION}/wasm_singlethread \
    QT_HOST=/opt/Qt/${QT_VERSION}/gcc_64

# ------------------------------------------------------------------ build
FROM toolchain AS build
COPY topology-editor /src/topology-editor
RUN ${QT_WASM}/bin/qt-cmake -S /src/topology-editor -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=MinSizeRel -DQT_HOST_PATH=${QT_HOST} \
 && cmake --build /build --parallel \
 && mkdir -p /out \
 && cp /build/topology.js /build/topology.wasm /build/qtloader.js /out/

# ------------------------------------------------------------------ runtime
FROM ${REGISTRY}/library/nginx:1.27-alpine AS runtime
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY web/ /usr/share/nginx/html/
COPY --from=build /out/ /usr/share/nginx/html/demo/topology/
# 填入 wasm 原始大小（加载页算进度用），然后预压缩：wasm 压缩后约为原来的 1/3，nginx 用 gzip_static 直接发送 .gz 文件
RUN H=/usr/share/nginx/html/demo/topology \
 && sed -i "s/__WASM_SIZE__/$(stat -c%s $H/topology.wasm)/" $H/index.html \
 && find /usr/share/nginx/html -type f \
        \( -name '*.wasm' -o -name '*.js' -o -name '*.html' -o -name '*.css' -o -name '*.svg' -o -name '*.json' \) \
        -exec gzip -9 -k {} \;
EXPOSE 80
