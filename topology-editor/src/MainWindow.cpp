#include "MainWindow.h"
#include "comm/CommManager.h"
#include "commands/Commands.h"
#include "dashboard/DashboardBackend.h"
#include "io/TopologyIO.h"
#include "history/HistoryDb.h"
#include "monitor/DeviceMonitor.h"
#include "model/AlarmLogModel.h"
#include "model/PropertyDelegate.h"
#include "model/PropertyModel.h"
#include "scene/EdgeItem.h"
#include "scene/MiniMapView.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"
#include "scene/TopologyView.h"
#include "sim/AlarmSimulator.h"
#include "widgets/DevicePanel.h"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QQuickView>
#include <QScreen>
#include <QTableView>
#include <QtQml/qqmlextensionplugin.h>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUndoView>

#ifndef Q_OS_WASM
#include <QFile>
#endif

// 大屏的 QML 模块编成了静态插件，需要显式导入
Q_IMPORT_QML_PLUGIN(Topology_DashboardPlugin)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_undo(new QUndoStack(this)),
      m_scene(new TopologyScene(m_undo, this)),
      m_view(new TopologyView(m_scene, this)),
      m_sim(new AlarmSimulator(m_scene, this))
{
    setWindowTitle(QStringLiteral("网络拓扑监控 · Qt 上位机演示"));
    m_undo->setUndoLimit(500);
    setCentralWidget(m_view);

    m_comm = new CommManager(this);
    m_monitor = new DeviceMonitor(m_scene, this);
    m_history = new HistoryManager(HistoryManager::defaultPath(), this);

    createActions();
    createDocks();
    createStatusBar();

    // 大屏的数据门面一开始就创建，趋势曲线在后台持续采样，打开大屏就有两分钟历史
    m_backend = new DashboardBackend(m_scene, m_monitor, m_comm, m_alarmLog, this);
    auto countAlarms = [this](const QVector<AlarmEvent> &events) {
        m_backend->countAlarmEvents(int(std::count_if(events.cbegin(), events.cend(), [](const AlarmEvent &e) {
            return e.level == NodeStatus::Alarm;
        })));
    };
    connect(m_monitor, &DeviceMonitor::alarmEvents, this, countAlarms);
    connect(m_sim, &AlarmSimulator::events, this, countAlarms);
    connect(m_backend, &DashboardBackend::backRequested, this, &MainWindow::closeDashboard);
    connect(m_backend, &DashboardBackend::fullScreenRequested, this, &MainWindow::toggleDashboardFullScreen);
    connect(m_backend, &DashboardBackend::connectRequested, m_devicePanel, &DevicePanel::connectVirtual);
    connect(m_backend, &DashboardBackend::locateRequested, this, [this](int id) {
#ifdef Q_OS_WASM
        closeDashboard();   // 浏览器里大屏盖住了编辑器，先关掉
#endif
        raise();
        activateWindow();
        locateNode(id);
    });

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_scene, &TopologyScene::message, this, [this](const QString &t) { statusBar()->showMessage(t, 3000); });
    connect(m_view, &TopologyView::viewportChanged, m_miniMap, &MiniMapView::markDirty);
    connect(m_undo, &QUndoStack::indexChanged, m_miniMap, &MiniMapView::markDirty);

    auto *statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, &MainWindow::updateStats);
    statsTimer->start(500);

    connect(m_comm, &CommManager::samplesReady, m_monitor, &DeviceMonitor::onSamples);
    connect(m_comm, &CommManager::samplesReady, m_history, &HistoryManager::appendSamples);
    connect(m_monitor, &DeviceMonitor::alarmEvents, m_history, [this](const QVector<AlarmEvent> &events) {
        QVector<HistoryAlarm> rows;
        rows.reserve(events.size());
        for (const AlarmEvent &e : events)
            rows.append({e.time.toMSecsSinceEpoch(), e.nodeId, e.nodeName, int(e.level), e.text});
        m_history->appendAlarms(rows);
    });
    connect(m_comm, &CommManager::stateChanged, this, &MainWindow::onLinkStateChanged);

    resize(1400, 900);
    QTimer::singleShot(0, this, &MainWindow::startDemo);
}

MainWindow::~MainWindow()
{
    // 场景析构时删除选中的节点会发 selectionChanged，此时 MainWindow 已经析构了一半，先断开
    disconnect(m_scene, nullptr, this, nullptr);
    if (m_dashboard) {
        m_dashboard->disconnect(this);
        delete m_dashboard;
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 大屏是独立窗口，不关掉的话主窗口关闭后程序不会退出
    closeDashboard();
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------- 动作/菜单

void MainWindow::createActions()
{
    // ----- 文件
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(QStringLiteral("新建"), QKeySequence::New, this, &MainWindow::newTopology);
    fileMenu->addAction(QStringLiteral("打开 JSON..."), QKeySequence::Open, this, &MainWindow::openTopology);
    fileMenu->addAction(QStringLiteral("导出 JSON..."), QKeySequence::Save, this, &MainWindow::saveTopology);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("加载示例：园区网络"), this, &MainWindow::loadSample);

    // ----- 编辑
    QAction *undoAct = m_undo->createUndoAction(this, QStringLiteral("撤销"));
    undoAct->setShortcut(QKeySequence::Undo);
    QAction *redoAct = m_undo->createRedoAction(this, QStringLiteral("重做"));
    redoAct->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});

    auto *deleteAct = new QAction(QStringLiteral("删除"), this);
    deleteAct->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    connect(deleteAct, &QAction::triggered, this, &MainWindow::deleteSelection);

    auto *selectAllAct = new QAction(QStringLiteral("全选"), this);
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAct, &QAction::triggered, this, [this] {
        for (NodeItem *n : m_scene->nodes())
            n->setSelected(true);
    });

    QMenu *editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    editMenu->addActions({undoAct, redoAct});
    editMenu->addSeparator();
    editMenu->addActions({deleteAct, selectAllAct});

    // ----- 模式
    m_modeGroup = new QActionGroup(this);
    struct ModeDef { const char *text; TopologyScene::Mode mode; Qt::Key key; };
    const ModeDef modes[] = {
        {"选择", TopologyScene::Mode::Select, Qt::Key_S},
        {"添加节点", TopologyScene::Mode::AddNode, Qt::Key_A},
        {"连线", TopologyScene::Mode::Connect, Qt::Key_C},
    };
    for (const ModeDef &m : modes) {
        QAction *a = m_modeGroup->addAction(QString::fromUtf8(m.text));
        a->setCheckable(true);
        a->setShortcut(m.key);
        a->setToolTip(QStringLiteral("%1 (%2)").arg(a->text(), QKeySequence(m.key).toString()));
        a->setData(int(m.mode));
    }
    m_modeGroup->actions().first()->setChecked(true);
    connect(m_modeGroup, &QActionGroup::triggered, this,
            [this](QAction *a) { m_scene->setMode(TopologyScene::Mode(a->data().toInt())); });

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems(nodeTypeNames());
    m_typeCombo->setCurrentIndex(int(NodeType::Server));
    m_typeCombo->setToolTip(QStringLiteral("添加节点时使用的设备类型"));
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_scene->setNodeTypeToAdd(NodeType(i));
        m_modeGroup->actions().at(1)->trigger();
    });

    // ----- 对齐
    QMenu *alignMenu = new QMenu(QStringLiteral("对齐"), this);
    alignMenu->addAction(QStringLiteral("左对齐"), this, [this] { alignSelection(Qt::AlignLeft); });
    alignMenu->addAction(QStringLiteral("右对齐"), this, [this] { alignSelection(Qt::AlignRight); });
    alignMenu->addAction(QStringLiteral("顶端对齐"), this, [this] { alignSelection(Qt::AlignTop); });
    alignMenu->addAction(QStringLiteral("底端对齐"), this, [this] { alignSelection(Qt::AlignBottom); });
    alignMenu->addAction(QStringLiteral("水平居中"), this, [this] { alignSelection(Qt::AlignHCenter); });
    alignMenu->addAction(QStringLiteral("垂直居中"), this, [this] { alignSelection(Qt::AlignVCenter); });
    editMenu->addSeparator();
    editMenu->addMenu(alignMenu);

    auto *snapAct = new QAction(QStringLiteral("吸附网格"), this);
    snapAct->setCheckable(true);
    snapAct->setChecked(m_scene->snapToGrid());
    connect(snapAct, &QAction::toggled, m_scene, &TopologyScene::setSnapToGrid);

    // ----- 视图
    auto *fitAct = new QAction(QStringLiteral("适应窗口"), this);
    fitAct->setShortcut(Qt::Key_F);
    connect(fitAct, &QAction::triggered, m_view, &TopologyView::fitAll);
    auto *zoom100Act = new QAction(QStringLiteral("100%"), this);
    zoom100Act->setShortcut(Qt::Key_1);
    connect(zoom100Act, &QAction::triggered, m_view, &TopologyView::resetZoom);

    m_dashAct = new QAction(QStringLiteral("监控大屏"), this);
    m_dashAct->setShortcut(Qt::Key_F10);
    m_dashAct->setCheckable(true);
    m_dashAct->setToolTip(QStringLiteral("打开 Qt Quick 监控大屏（F10；大屏里 F11 全屏，Esc 关闭）"));
    connect(m_dashAct, &QAction::triggered, this, [this](bool on) { on ? showDashboard() : closeDashboard(); });

    m_viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    m_viewMenu->addAction(m_dashAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addActions({fitAct, zoom100Act});
    m_viewMenu->addSeparator();

    // ----- 性能
    auto *lodAct = new QAction(QStringLiteral("LOD 分级绘制"), this);
    lodAct->setCheckable(true);
    lodAct->setChecked(NodeItem::lodEnabled);
    lodAct->setToolTip(QStringLiteral("关闭后无论缩放多少都完整绘制，可对比单帧耗时"));
    connect(lodAct, &QAction::toggled, this, [this](bool on) {
        NodeItem::lodEnabled = on;
        m_scene->update();
    });
    auto *bspAct = new QAction(QStringLiteral("BSP 索引"), this);
    bspAct->setCheckable(true);
    bspAct->setChecked(true);
    bspAct->setToolTip(QStringLiteral("关闭后场景使用线性查找，可对比拖动/框选的流畅度"));
    connect(bspAct, &QAction::toggled, m_scene, &TopologyScene::setBspIndexEnabled);

    auto *stressMenu = new QMenu(QStringLiteral("压力测试"), this);
    for (int n : {1000, 5000, 10000, 20000})
        stressMenu->addAction(QStringLiteral("%1 个节点").arg(n), this, [this, n] { runStressTest(n); });

    // ----- 模拟
    m_simAction = new QAction(QStringLiteral("告警模拟"), this);
    m_simAction->setCheckable(true);
    m_simAction->setShortcut(Qt::Key_Space);
    connect(m_simAction, &QAction::toggled, this, [this](bool on) { on ? m_sim->start() : m_sim->stop(); });
    auto *resetAct = new QAction(QStringLiteral("全部恢复"), this);
    connect(resetAct, &QAction::triggered, m_sim, &AlarmSimulator::resetAll);

    QMenu *simMenu = menuBar()->addMenu(QStringLiteral("监控(&M)"));
    simMenu->addActions({m_simAction, resetAct});
    simMenu->addSeparator();
    simMenu->addMenu(stressMenu);
    simMenu->addSeparator();
    simMenu->addActions({lodAct, bspAct});

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(QStringLiteral("操作说明"), QKeySequence::HelpContents, this, &MainWindow::showHelp);
    helpMenu->addAction(QStringLiteral("关于"), this, &MainWindow::showAbout);

    // ----- 工具栏
    QToolBar *tb = addToolBar(QStringLiteral("编辑"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->addActions(m_modeGroup->actions());
    tb->addWidget(m_typeCombo);
    tb->addSeparator();
    tb->addActions({undoAct, redoAct, deleteAct});
    // 下拉按钮不用 setMenu()+InstantPopup：那样内部走 QMenu::exec() 嵌套事件循环，
    // WASM 单线程版不支持，会卡死。改用非阻塞的 popup()。
    auto menuButton = [this](QMenu *menu) {
        auto *btn = new QToolButton(this);
        btn->setText(menu->title() + QStringLiteral(" ▾"));
        connect(btn, &QToolButton::clicked, menu, [btn, menu] {
            menu->popup(btn->mapToGlobal(QPoint(0, btn->height())));
        });
        return btn;
    };
    tb->addWidget(menuButton(alignMenu));
    tb->addAction(snapAct);
    tb->addSeparator();
    tb->addActions({fitAct, zoom100Act});
    tb->addSeparator();
    tb->addAction(m_simAction);
    tb->addAction(resetAct);
    tb->addWidget(menuButton(stressMenu));
    tb->addActions({lodAct, bspAct});
    tb->addSeparator();
    tb->addAction(m_dashAct);
}

void MainWindow::createDocks()
{
    setDockOptions(AnimatedDocks | AllowTabbedDocks);

    // 属性
    m_props = new PropertyModel(m_undo, this);
    auto *propView = new QTableView;
    propView->setModel(m_props);
    propView->setItemDelegate(new PropertyDelegate(propView));
    propView->verticalHeader()->hide();
    propView->horizontalHeader()->setStretchLastSection(true);
    propView->setColumnWidth(0, 70);
    propView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    propView->setSelectionMode(QAbstractItemView::SingleSelection);
    auto *propDock = new QDockWidget(QStringLiteral("属性"), this);
    propDock->setWidget(propView);
    addDockWidget(Qt::RightDockWidgetArea, propDock);

    // 撤销历史
    auto *undoView = new QUndoView(m_undo);
    undoView->setEmptyLabel(QStringLiteral("<初始状态>"));
    auto *undoDock = new QDockWidget(QStringLiteral("撤销历史"), this);
    undoDock->setWidget(undoView);
    addDockWidget(Qt::RightDockWidgetArea, undoDock);
    tabifyDockWidget(propDock, undoDock);
    propDock->raise();

    // 鸟瞰图
    m_miniMap = new MiniMapView(m_view);
    auto *mapDock = new QDockWidget(QStringLiteral("鸟瞰图"), this);
    mapDock->setWidget(m_miniMap);
    addDockWidget(Qt::RightDockWidgetArea, mapDock);

    // 告警日志
    m_alarmLog = new AlarmLogModel(this);
    m_alarmProxy = new AlarmFilterProxy(this);
    m_alarmProxy->setSourceModel(m_alarmLog);

    auto *levelCombo = new QComboBox;
    levelCombo->addItems({QStringLiteral("全部级别"), QStringLiteral("警告及以上"), QStringLiteral("仅告警/离线")});
    connect(levelCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        static const int levels[] = {-1, int(NodeStatus::Warning), int(NodeStatus::Alarm)};
        m_alarmProxy->setMinimumLevel(levels[i]);
    });
    auto *search = new QLineEdit;
    search->setPlaceholderText(QStringLiteral("按节点名或描述过滤…"));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, m_alarmProxy, &AlarmFilterProxy::setKeyword);
    auto *clearBtn = new QToolButton;
    clearBtn->setText(QStringLiteral("清空"));
    connect(clearBtn, &QToolButton::clicked, m_alarmLog, &AlarmLogModel::clear);

    auto *alarmView = new QTableView;
    alarmView->setModel(m_alarmProxy);
    alarmView->verticalHeader()->hide();
    alarmView->verticalHeader()->setDefaultSectionSize(22);
    alarmView->horizontalHeader()->setStretchLastSection(true);
    alarmView->setColumnWidth(AlarmLogModel::ColTime, 110);
    alarmView->setColumnWidth(AlarmLogModel::ColNode, 140);
    alarmView->setColumnWidth(AlarmLogModel::ColLevel, 60);
    alarmView->setSelectionBehavior(QAbstractItemView::SelectRows);
    alarmView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alarmView->setToolTip(QStringLiteral("双击定位到节点"));
    connect(alarmView, &QTableView::doubleClicked, this,
            [this](const QModelIndex &idx) { locateNode(idx.data(AlarmLogModel::NodeIdRole).toInt()); });

    auto *alarmPanel = new QWidget;
    auto *bar = new QHBoxLayout;
    bar->addWidget(levelCombo);
    bar->addWidget(search, 1);
    bar->addWidget(clearBtn);
    auto *vbox = new QVBoxLayout(alarmPanel);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->addLayout(bar);
    vbox->addWidget(alarmView);
    auto *alarmDock = new QDockWidget(QStringLiteral("告警日志"), this);
    alarmDock->setWidget(alarmPanel);
    addDockWidget(Qt::BottomDockWidgetArea, alarmDock);

    connect(m_sim, &AlarmSimulator::events, m_alarmLog, &AlarmLogModel::append);
    connect(m_monitor, &DeviceMonitor::alarmEvents, m_alarmLog, &AlarmLogModel::append);

    // 设备通信：和告警日志并排放在底部
    m_devicePanel = new DevicePanel(m_comm, m_monitor, m_history);
    m_devicePanel->setDeviceIdProvider([this] { return virtualDeviceIds(); });
    auto *deviceDock = new QDockWidget(QStringLiteral("设备通信"), this);
    deviceDock->setWidget(m_devicePanel);
    addDockWidget(Qt::BottomDockWidgetArea, deviceDock);
    splitDockWidget(alarmDock, deviceDock, Qt::Horizontal);

    resizeDocks({propDock, mapDock}, {360, 220}, Qt::Vertical);
    resizeDocks({propDock}, {300}, Qt::Horizontal);
    resizeDocks({alarmDock}, {330}, Qt::Vertical);
    resizeDocks({alarmDock, deviceDock}, {480, 920}, Qt::Horizontal);

    for (QDockWidget *d : {propDock, undoDock, mapDock, alarmDock, deviceDock})
        m_viewMenu->addAction(d->toggleViewAction());
}

void MainWindow::createStatusBar()
{
    m_statCount = new QLabel;
    m_statStatus = new QLabel;
    m_statZoom = new QLabel;
    m_statFrame = new QLabel;
    m_statFrame->setToolTip(QStringLiteral("主视图最近若干帧的平均渲染耗时"));
    m_statLink = new QLabel;
    m_statLink->setToolTip(QStringLiteral("3 秒内收到过数据的设备数"));
    for (QLabel *l : {m_statLink, m_statCount, m_statStatus, m_statZoom, m_statFrame}) {
        l->setContentsMargins(8, 0, 8, 0);
        statusBar()->addPermanentWidget(l);
    }
}

// ---------------------------------------------------------------- 文件

void MainWindow::resetForRebuild()
{
    // 顺序很重要：先清撤销栈（释放被命令持有的图元），再清场景
    m_props->setNode(nullptr);
    m_undo->clear();
    m_monitor->reset(false);   // 节点马上要被清掉，不必恢复颜色
}

void MainWindow::afterRebuild()
{
    // 虚拟设备按拓扑生成，拓扑换了就用新的设备列表重连
    m_devicePanel->restartIfVirtual();
}

QList<int> MainWindow::virtualDeviceIds() const
{
    // 云节点不是被管设备；上万节点压测时只模拟前 300 台，避免数据量失控
    QList<int> ids;
    for (NodeItem *n : m_scene->nodes())
        if (n->nodeType() != NodeType::Cloud)
            ids << n->id();
    std::sort(ids.begin(), ids.end());
    if (ids.size() > 300)
        ids.resize(300);
    return ids;
}

void MainWindow::startDemo()
{
    loadSample();
    m_devicePanel->connectVirtual();
    // 默认选中一台核心设备，打开页面就能看到实时曲线
    for (NodeItem *n : m_scene->nodes()) {
        if (n->nodeType() == NodeType::Router) {
            n->setSelected(true);
            break;
        }
    }
}

void MainWindow::onLinkStateChanged(LinkState state)
{
    // 链路接通后节点状态由真实数据驱动，随机告警模拟让位
    const bool active = state != LinkState::Disconnected;
    if (active && m_simAction->isChecked())
        m_simAction->setChecked(false);
    m_simAction->setEnabled(!active);
    m_simAction->setToolTip(active ? QStringLiteral("设备链路已连接，告警由实时数据驱动；断开链路后可用")
                                   : QStringLiteral("随机改变节点状态（不经过通信链路）"));
}

void MainWindow::newTopology()
{
    resetForRebuild();
    m_scene->clearTopology();
    m_alarmLog->clear();
    m_view->resetZoom();
    m_view->centerOn(0, 0);
    afterRebuild();
}

void MainWindow::openTopology()
{
#ifdef Q_OS_WASM
    // 浏览器沙箱里没有文件路径，只能拿到文件内容（异步回调）
    QFileDialog::getOpenFileContent(QStringLiteral("拓扑文件 (*.json)"),
                                    [this](const QString &fileName, const QByteArray &content) {
                                        if (!fileName.isEmpty())
                                            loadBytes(content, fileName);
                                    });
#else
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开拓扑"), QString(),
                                                      QStringLiteral("拓扑文件 (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(QStringLiteral("无法打开 %1").arg(path), 5000);
        return;
    }
    loadBytes(f.readAll(), path);
#endif
}

void MainWindow::saveTopology()
{
    const QByteArray data = TopologyIO::toJson(m_scene);
#ifdef Q_OS_WASM
    // 触发浏览器下载
    QFileDialog::saveFileContent(data, QStringLiteral("topology.json"));
#else
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出拓扑"), QStringLiteral("topology.json"),
                                                      QStringLiteral("拓扑文件 (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
        statusBar()->showMessage(QStringLiteral("写入失败：%1").arg(path), 5000);
        return;
    }
#endif
    statusBar()->showMessage(QStringLiteral("已导出 %1 个节点、%2 条连线")
                                 .arg(m_scene->nodes().size())
                                 .arg(m_scene->edges().size()),
                             4000);
}

void MainWindow::loadBytes(const QByteArray &data, const QString &source)
{
    resetForRebuild();
    QString error;
    if (!TopologyIO::fromJson(m_scene, data, &error)) {
        auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("导入失败"), error, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();   // 非阻塞，WASM 下不能用 exec()
        return;
    }
    m_alarmLog->clear();
    m_view->fitAll();
    afterRebuild();
    statusBar()->showMessage(QStringLiteral("已加载 %1：%2 个节点").arg(source).arg(m_scene->nodes().size()), 4000);
}

void MainWindow::loadSample()
{
    resetForRebuild();
    TopologyIO::buildSampleCampus(m_scene);
    m_alarmLog->clear();
    m_view->fitAll();
    afterRebuild();
}

void MainWindow::runStressTest(int count)
{
    resetForRebuild();
    m_alarmLog->clear();
    QElapsedTimer t;
    t.start();
    TopologyIO::buildStressGrid(m_scene, count);
    const qint64 buildMs = t.elapsed();
    m_view->fitAll();
    afterRebuild();
    statusBar()->showMessage(QStringLiteral("已生成 %1 个节点、%2 条连线，用时 %3 ms。滚轮放大可看到 LOD 切换。")
                                 .arg(m_scene->nodes().size())
                                 .arg(m_scene->edges().size())
                                 .arg(buildMs),
                             8000);
}

// ---------------------------------------------------------------- 编辑

void MainWindow::deleteSelection()
{
    const auto nodes = m_scene->selectedNodes();
    const auto edges = m_scene->selectedEdges();
    if (nodes.isEmpty() && edges.isEmpty())
        return;
    m_props->setNode(nullptr);
    m_undo->push(new DeleteItemsCommand(m_scene, nodes, edges));
}

void MainWindow::alignSelection(Qt::Alignment how)
{
    const auto nodes = m_scene->selectedNodes();
    if (nodes.size() < 2) {
        statusBar()->showMessage(QStringLiteral("至少选中两个节点才能对齐"), 3000);
        return;
    }
    qreal minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (NodeItem *n : nodes) {
        minX = std::min(minX, n->x());
        maxX = std::max(maxX, n->x());
        minY = std::min(minY, n->y());
        maxY = std::max(maxY, n->y());
    }
    QList<QPointF> from, to;
    for (NodeItem *n : nodes) {
        QPointF p = n->pos();
        from << p;
        if (how & Qt::AlignLeft)    p.setX(minX);
        if (how & Qt::AlignRight)   p.setX(maxX);
        if (how & Qt::AlignHCenter) p.setX((minX + maxX) / 2);
        if (how & Qt::AlignTop)     p.setY(minY);
        if (how & Qt::AlignBottom)  p.setY(maxY);
        if (how & Qt::AlignVCenter) p.setY((minY + maxY) / 2);
        to << p;
    }
    m_undo->push(new MoveNodesCommand(nodes, from, to, QStringLiteral("对齐 %1 个节点").arg(nodes.size())));
}

void MainWindow::onSelectionChanged()
{
    const auto nodes = m_scene->selectedNodes();
    m_props->setNode(nodes.size() == 1 ? nodes.first() : nullptr);
    if (nodes.size() == 1)
        m_devicePanel->setCurrentDevice(nodes.first()->id(), nodes.first()->name());
}

void MainWindow::locateNode(int id)
{
    NodeItem *n = m_scene->node(id);
    if (!n) {
        statusBar()->showMessage(QStringLiteral("节点 %1 已不存在").arg(id), 3000);
        return;
    }
    m_scene->clearSelection();
    n->setSelected(true);
    if (m_view->zoom() < 0.6)
        m_view->zoomBy(0.8 / m_view->zoom());
    m_view->centerOn(n);
}

void MainWindow::updateStats()
{
    m_statCount->setText(QStringLiteral("节点 %1 · 连线 %2").arg(m_scene->nodes().size()).arg(m_scene->edges().size()));
    m_statStatus->setText(QStringLiteral("<span style='color:#2ea043'>正常 %1</span> · "
                                         "<span style='color:#e09b10'>警告 %2</span> · "
                                         "<span style='color:#d93025'>告警 %3</span> · "
                                         "<span style='color:#8a8f98'>离线 %4</span>")
                              .arg(m_scene->statusCount(NodeStatus::Normal))
                              .arg(m_scene->statusCount(NodeStatus::Warning))
                              .arg(m_scene->statusCount(NodeStatus::Alarm))
                              .arg(m_scene->statusCount(NodeStatus::Offline)));
    m_statZoom->setText(QStringLiteral("缩放 %1%").arg(qRound(m_view->zoom() * 100)));
    m_statFrame->setText(QStringLiteral("渲染 %1 ms/帧").arg(m_view->averageFrameMs(), 0, 'f', 1));
    if (m_comm->state() == LinkState::Connected) {
        const int monitored = int(virtualDeviceIds().size());
        m_statLink->setText(QStringLiteral("在线设备 %1").arg(m_monitor->onlineCount()) +
                            (m_comm->kind() == LinkConfig::Virtual ? QStringLiteral("/%1").arg(monitored) : QString()));
    } else {
        m_statLink->setText(QStringLiteral("链路未连接"));
    }
    if (m_sim->isRunning() || m_comm->state() == LinkState::Connected)
        m_props->refresh();
}

// ---------------------------------------------------------------- 监控大屏

void MainWindow::showDashboard()
{
    if (m_dashboard) {
        m_dashboard->raise();
        m_dashboard->requestActivate();
        m_dashAct->setChecked(true);
        return;
    }
    // 打开时才创建 QML 引擎和界面，不拖慢启动（WASM 版尤其明显）。
    // 用独立窗口而不是嵌进主窗口的 QQuickWidget：WASM 版主窗口一旦含有 QQuickWidget，
    // 整个窗口改走 GPU 合成，Qt 6.8 在这条路径上局部刷新普通控件会持续报 WebGL 纹理上传错误。
    m_dashboard = new QQuickView;
    m_dashboard->setTitle(QStringLiteral("网络运行监控中心"));
    m_dashboard->setColor(QColor(0x06, 0x0c, 0x18));
    m_dashboard->setResizeMode(QQuickView::SizeRootObjectToView);
    m_dashboard->loadFromModule("Topology.Dashboard", "Dashboard");
    if (m_dashboard->status() == QQuickView::Error) {
        QStringList lines;
        for (const QQmlError &e : m_dashboard->errors())
            lines << e.toString();
        qWarning().noquote() << "大屏加载失败:" << lines.join(QLatin1Char('\n'));
        statusBar()->showMessage(QStringLiteral("大屏加载失败，详见控制台输出"), 5000);
        delete m_dashboard;
        m_dashboard = nullptr;
        m_dashAct->setChecked(false);
        return;
    }
    // 用户直接点窗口的关闭按钮时同样走 closeDashboard，释放资源并同步菜单勾选状态
    connect(m_dashboard, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible)
            closeDashboard();
    });

    m_backend->setActive(true);
    m_dashAct->setChecked(true);
#ifdef Q_OS_WASM
    m_dashboard->showFullScreen();   // 填满页面里的 Qt 容器，盖住编辑器
#else
    // 在主窗口所在的屏幕上打开；多屏时可以拖到大屏显示器再按 F11
    m_dashboard->setScreen(screen());
    m_dashboard->resize(1600, 900);
    m_dashboard->setPosition(screen()->availableGeometry().center() - QPoint(800, 450));
    m_dashboard->show();
#endif
}

void MainWindow::closeDashboard()
{
    m_dashAct->setChecked(false);
    if (!m_dashboard)
        return;
    // 数据和趋势历史在 C++ 后端里，关掉界面不丢；下次打开重新创建
    QQuickView *view = std::exchange(m_dashboard, nullptr);
    m_backend->setActive(false);
    view->disconnect(this);
    view->deleteLater();
    activateWindow();
}

void MainWindow::toggleDashboardFullScreen()
{
    if (!m_dashboard)
        return;
    if (m_dashboard->visibility() == QWindow::FullScreen)
        m_dashboard->showNormal();
    else
        m_dashboard->showFullScreen();
}

// ---------------------------------------------------------------- 帮助

void MainWindow::showHelp()
{
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QStringLiteral("操作说明"));
    box->setTextFormat(Qt::RichText);
    box->setText(QStringLiteral(
        "<b>模式</b>：S 选择 · A 添加节点（点空白处）· C 连线（从一个节点拖到另一个）<br>"
        "<b>视图</b>：滚轮缩放 · 中键或 Alt+左键拖动平移 · F 适应窗口 · 1 原始大小<br>"
        "<b>编辑</b>：框选 · 方向键微调（Shift 加速）· Delete 删除 · Ctrl+Z / Ctrl+Y<br>"
        "<b>设备通信</b>：启动后自动连接应用内虚拟设备；选中节点查看实时曲线 · "
        "曲线上滚轮缩放、拖动平移、双击回到实时 · 可注入分包/误码/丢应答/断线故障 · "
        "参数下发页可改设备负载触发告警 · 仪表盘显示选中设备实时读数 · "
        "历史查询页从 SQLite 回放曲线、查看告警、导出 CSV · 桌面版可通过 TCP/串口连接独立模拟器 devsim<br>"
        "<b>监控大屏</b>：F10 打开 Qt Quick 大屏（独立窗口），大屏里 F11 全屏、Esc 关闭；点告警或设备卡片在拓扑图中定位<br>"
        "<b>监控</b>：断开链路后，空格开关随机告警模拟 · 双击告警日志定位节点<br>"
        "<b>性能</b>：压力测试生成上万节点，开关 LOD / BSP 对比状态栏里的单帧耗时"));
    box->open();
}

void MainWindow::showAbout()
{
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QStringLiteral("关于"));
    box->setTextFormat(Qt::RichText);
    box->setText(QStringLiteral("<h3>网络拓扑监控</h3>"
                                "<p>C++17 · Qt %1 · Graphics View · Qt Quick · Model/View · QThread · 自定义协议 · SQLite</p>"
                                "<p>同一套代码编译桌面版与 WebAssembly 版。</p>")
                     .arg(QString::fromLatin1(qVersion())));
    box->open();
}
