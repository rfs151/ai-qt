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

### 3.3 上位机能力：设备通信 / 多线程 / 实时曲线

```
设备端                         通信线程 comm-worker                    界面线程
VirtualDevice(QIODevice) ─┐    CommWorker                               DeviceMonitor ─> 节点状态/链路负载/告警日志
QTcpSocket ─> devsim      ├──> FrameParser 解帧 ─> 采样攒批(33ms) ──信号──> TelemetryStore(环形缓冲)
QSerialPort ─> devsim     ┘    心跳超时/指数退避重连、写参数超时重发          RealtimeChart / DevicePanel
```

- **协议**：`AA 55 | cmd | seq | len | payload | CRC16/MODBUS`；解析器处理半包、粘包、垃圾字节、CRC 错误、离谱长度，单元测试逐字节喂入验证。
- **链路**：虚拟设备 / TCP / 串口三种实现都是 `QIODevice`，上层代码不区分。虚拟设备可注入分包粘包、误码、丢应答、断线。
- **线程**：`CommWorker` `moveToThread` 到独立线程，`CommManager` 用 `QueuedConnection` 转发调用，信号自动跨线程排队；采样按 30Hz 攒批投递，界面不会被高频数据拖垮。WASM 单线程版（`QT_CONFIG(thread)` 为假）工作对象留在主线程，接口不变。
- **状态机**：未连接 → 连接中 → 已连接 → 重连中；3 秒无有效帧判定心跳超时，重连间隔 0.5s 起指数退避到 8s。
- **写参数**：seq 匹配应答，300ms 超时，同一 seq 重发 2 次（设备端幂等），失败上报。
- **告警判定**：CPU/温度阈值 + 5 的回差，避免阈值附近抖动；3 秒无数据判离线，恢复时记录"通信恢复"。
- **曲线**：QPainter 自绘，多分区共享时间轴；min/max 降采样（20 万点 → 约 3000 点，峰值不丢）；数据中断处断开折线；暂停后滚轮缩放、拖动平移，十字游标读数。
- **devsim**：独立的模拟器进程，`devsim --tcp 9000` 或 `devsim --serial /dev/pts/N`（可用 `socat -d -d pty,raw,echo=0 pty,raw,echo=0` 造虚拟串口对）。
- **测试**：`tst_comm` 覆盖 CRC、解析器各种异常输入、环形缓冲、降采样、跨线程采样投递、重发超时、误码不掉线、断线重连，以及启动真实 devsim 进程的 TCP 端到端测试。

### 3.4 仪表盘与 SQLite 历史存储

- **仪表盘**（`widgets/GaugeWidget`）：QPainter 自绘 240° 表盘，正常/警告/告警三段色带取自监控阈值。表盘静态部分（色带、刻度、数字、标题）在尺寸或量程变化时画一次缓存为 QPixmap，每帧只画指针和读数；指针做指数缓动，到位后停掉定时器。仪表页不可见时不刷新。
- **写库线程**（`history/HistoryDb`）：`HistoryWorker` 运行在低优先级线程 `history-writer`，QSqlDatabase 连接在该线程内创建和使用。网页版没有线程，退化为主线程 + `:memory:` 数据库。
- **入库策略**：遥测按"设备 × 1 秒"聚合（CPU/温度取最大值保留峰值，流量取平均），38 台 10Hz 设备从 380 行/秒降到 38 行/秒；每秒一个事务批量提交；WAL + `synchronous=NORMAL`。
- **表结构**：`samples(device_id, ts_ms, ...)` 以 `(device_id, ts_ms)` 为主键并 `WITHOUT ROWID`，数据按设备+时间聚簇，区间查询是连续扫描；`ts_ms` 单独索引供过期清理；`alarms` 表按 `(device_id, ts_ms)` 建索引。
- **未完成的一秒**：查询前先把正在累积的这一秒写入，之后用 `INSERT OR REPLACE` 覆盖成完整值；迟到数据继续累加，不会产生重复行。
- **查询**：点数超过上限时在 SQL 里 `GROUP BY ts_ms / 桶宽` 聚合，只把结果搬进内存；曲线复用 RealtimeChart 的回放模式（固定范围、缩放平移、双击复位）。
- **其他**：默认保留 24 小时（每分钟清理）；CSV 导出带 BOM（Excel 中文不乱码），网页版走浏览器下载；`tst_history` 覆盖聚合、部分秒补全、SQL 聚合保峰值、告警与过期清理、跨线程写入后重启可查。

### 3.5 Modbus RTU / TCP

- **自己实现编解码**（`comm/Modbus`）：不依赖 QtSerialBus，浏览器版也能跑；单元测试里用 QtSerialBus 的 `QModbusTcpServer` / `QModbusTcpClient` 做双向互操作验证。
- **寄存器表**：每台设备一个从站（地址 = 设备 ID）。输入寄存器 30001 CPU（uint16 ×0.1）、30002 温度（int16 ×0.1）、30003-30006 入/出流量（float32，高字在前 ABCD）；保持寄存器 40001 采样周期、40002 负载偏置。
- **主站**（`ModbusMaster`，运行在通信线程）：一问一答（RTU 半双工），每轮依次 FC04 读所有从站，正在查看的从站加读 FC03；写操作插队优先，300ms 超时重发 2 次；从站连续 3 次无应答暂停轮询 5 秒，避免拖慢整条总线；广播地址 0 没有应答无法确认，改为逐台写并汇总结果。
- **RTU 切帧**：RTU 没有长度字段，按功能码推算帧长（读应答看字节数，写应答固定 8 字节，异常 5 字节）；应答必须以当前从站地址开头，CRC 低字节在前。TCP 用 MBAP 头的长度切帧，按事务号丢弃迟到应答。
- **从站**（`ModbusSlaveSim`）：支持 FC03/04/06/16，返回异常码 01/02/03；FC16 先整体校验再写入；RTU 下不存在的地址和广播不应答，TCP 网关对不存在的单元返回 0x0B。
- **接入方式**：虚拟设备（RTU）、串口（RTU）、TCP（Modbus TCP）；`devsim --modbus --tcp 1502` 或 `--serial` 作为外部从站。
- **界面**：协议切换、从站范围与轮询周期；“寄存器”页显示选中从站的原始寄存器和工程值；报文监视按从站/功能码解码；参数下发映射为写保持寄存器。

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

- GitHub Actions 自动构建镜像推送到镜像仓库，VPS 只需 `docker compose pull`。
