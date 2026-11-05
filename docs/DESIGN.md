# Qt 技能展示站 —— 设计方案

## 1. 目标

用一个可在浏览器里直接操作的 Qt 程序展示 C++ / Qt Widgets / Graphics View 能力。
面试官打开链接就能操作，不用下载安装。同一套代码同时编译桌面版和 Web 版。

## 2. 总体架构

```
浏览器 ──HTTP(S)──> VPS: docker compose
                        ├── web   (nginx:alpine)  静态站点 + 预压缩 wasm
                        └── caddy (可选)          自动 HTTPS 证书

镜像构建（多阶段 Dockerfile，本机或 CI 上执行）
  stage toolchain : emsdk 3.1.56 + Qt 6.8.3 (wasm_singlethread + gcc_64 host 工具)
  stage build     : qt-cmake 编译 topology-editor -> .wasm/.js/qtloader.js
  stage runtime   : nginx + 作品集首页 + 编译产物（gzip 预压缩）
```

选择理由：

| 决策 | 原因 |
| --- | --- |
| Qt 6.8 LTS + 单线程 wasm | 多线程版需要 COOP/COEP 响应头，单线程版部署简单，任何静态托管都能跑 |
| 编译环境放进 Docker | 本机和 VPS 都不用装 Qt/emsdk，版本固定（emsdk 必须和 Qt 版本严格匹配） |
| 在本机构建镜像，传到 VPS | 小内存 VPS 下载 Qt 工具链（约 3GB）并编译比较吃力；`deploy.sh` 用 `docker save | ssh docker load` 传输 |
| nginx gzip_static | wasm 约 15MB，gzip 预压缩后约 5.7MB，不消耗 VPS CPU |
| 中文字体子集打包进 qrc | WASM 没有系统字体；只保留 GB2312 一级常用字 + 源码用到的字，体积约 1MB |

## 3. 演示项目：网络拓扑编辑器 / 监控台

一个项目把下面这些能力串起来，比零散的小 demo 更能体现架构能力。

### 3.1 模块划分

```
topology-editor/src
├── main.cpp                 字体加载、平台差异
├── MainWindow               菜单/工具栏/停靠窗口装配，动作统一管理
├── scene/
│   ├── NodeItem             自定义 QGraphicsItem：LOD 三级绘制、状态色、告警闪烁
│   ├── EdgeItem             连线：跟随节点、负载着色、LOD
│   ├── TopologyScene        编辑模式（选择/添加/连线）、网格吸附、批量更新
│   ├── TopologyView         滚轮缩放（以鼠标为中心）、中键平移、FPS 统计
│   └── MiniMapView          共享同一个 scene 的鸟瞰图，定时刷新而不是实时刷新
├── commands/
│   └── Commands             QUndoCommand：添加/删除/移动(合并)/连线/改属性/对齐
├── model/
│   ├── PropertyModel        QAbstractTableModel，编辑选中节点属性
│   ├── PropertyDelegate     按属性类型创建 QComboBox / QSpinBox / QLineEdit
│   └── AlarmLogModel        告警日志，配合 QSortFilterProxyModel 过滤
├── sim/
│   └── AlarmSimulator       QTimer 驱动状态变化，批量提交
└── io/
    └── TopologyIO           QJsonDocument 导入导出；WASM 用 getOpenFileContent/saveFileContent
```

### 3.2 技术要点（展示页上逐条讲解）

1. **一万节点保持流畅**
   - `BspTreeIndex` 场景索引，视口裁剪只绘制可见项；
   - `levelOfDetailFromTransform` 三级 LOD：缩小时节点退化为色块，不绘制文字和图标；
   - 告警闪烁由一个全局定时器驱动，只 `update()` 处于告警状态的节点，不刷整个场景；
   - `ItemSendsGeometryChanges` 只在节点上开启，连线位置在节点移动时增量更新；
   - 鸟瞰图共享 scene，200ms 节流刷新，避免每帧重绘两次。
2. **撤销重做**：移动命令用 `mergeWith` 合并连续拖动；删除节点时把关联连线一起记录，撤销能完整恢复；对齐操作用宏命令。
3. **Model/View**：属性面板是自定义 model + delegate，编辑结果走 QUndoStack；告警列表用代理模型做过滤。
4. **跨平台**：`#ifdef Q_OS_WASM` 隔离文件读写差异，其余代码完全共享。

## 4. 目录结构

```
ai-qt/
├── topology-editor/         Qt 工程（CMake）
├── web/                     作品集首页 + demo 外壳页
├── docker/                  Dockerfile、nginx.conf、Caddyfile
├── scripts/                 字体子集化、本地编译、部署脚本
├── docker-compose.yml       VPS 部署（HTTP）
└── docker-compose.https.yml 叠加 Caddy 自动 HTTPS
```

## 5. 部署流程

```
本机:  ./scripts/deploy.sh user@vps          # 构建镜像 -> 传输 -> 远程 compose up
VPS:   docker compose up -d                  # 只用 HTTP
       DOMAIN=qt.example.com docker compose -f docker-compose.yml -f docker-compose.https.yml up -d
```

## 6. 后续可扩展

- 再加一个"实时曲线 / 仪表盘"自绘控件 demo（QPainter、双缓冲），放进同一个站点；
- GitHub Actions 自动构建镜像推送到镜像仓库，VPS 只需 `docker compose pull`。
