#include "MainWindow.h"
#include "commands/Commands.h"
#include "io/TopologyIO.h"
#include "model/AlarmLogModel.h"
#include "model/PropertyDelegate.h"
#include "model/PropertyModel.h"
#include "scene/EdgeItem.h"
#include "scene/MiniMapView.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"
#include "scene/TopologyView.h"
#include "sim/AlarmSimulator.h"

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
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUndoView>

#ifndef Q_OS_WASM
#include <QFile>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_undo(new QUndoStack(this)),
      m_scene(new TopologyScene(m_undo, this)),
      m_view(new TopologyView(m_scene, this)),
      m_sim(new AlarmSimulator(m_scene, this))
{
    setWindowTitle(QStringLiteral("网络拓扑编辑器 · Qt Graphics View 演示"));
    m_undo->setUndoLimit(500);
    setCentralWidget(m_view);

    createActions();
    createDocks();
    createStatusBar();

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_scene, &TopologyScene::message, this, [this](const QString &t) { statusBar()->showMessage(t, 3000); });
    connect(m_view, &TopologyView::viewportChanged, m_miniMap, &MiniMapView::markDirty);
    connect(m_undo, &QUndoStack::indexChanged, m_miniMap, &MiniMapView::markDirty);

    auto *statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, &MainWindow::updateStats);
    statsTimer->start(500);

    resize(1400, 860);
    QTimer::singleShot(0, this, &MainWindow::loadSample);
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

    m_viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
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

    resizeDocks({propDock, mapDock}, {360, 220}, Qt::Vertical);
    resizeDocks({propDock}, {300}, Qt::Horizontal);
    resizeDocks({alarmDock}, {200}, Qt::Vertical);

    for (QDockWidget *d : {propDock, undoDock, mapDock, alarmDock})
        m_viewMenu->addAction(d->toggleViewAction());
}

void MainWindow::createStatusBar()
{
    m_statCount = new QLabel;
    m_statStatus = new QLabel;
    m_statZoom = new QLabel;
    m_statFrame = new QLabel;
    m_statFrame->setToolTip(QStringLiteral("主视图最近若干帧的平均渲染耗时"));
    for (QLabel *l : {m_statCount, m_statStatus, m_statZoom, m_statFrame}) {
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
}

void MainWindow::newTopology()
{
    resetForRebuild();
    m_scene->clearTopology();
    m_alarmLog->clear();
    m_view->resetZoom();
    m_view->centerOn(0, 0);
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
    statusBar()->showMessage(QStringLiteral("已加载 %1：%2 个节点").arg(source).arg(m_scene->nodes().size()), 4000);
}

void MainWindow::loadSample()
{
    resetForRebuild();
    TopologyIO::buildSampleCampus(m_scene);
    m_alarmLog->clear();
    m_view->fitAll();
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
    if (m_sim->isRunning())
        m_props->refresh();
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
        "<b>监控</b>：空格开关告警模拟 · 双击告警日志定位节点<br>"
        "<b>性能</b>：压力测试生成上万节点，开关 LOD / BSP 对比状态栏里的单帧耗时"));
    box->open();
}

void MainWindow::showAbout()
{
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QStringLiteral("关于"));
    box->setTextFormat(Qt::RichText);
    box->setText(QStringLiteral("<h3>网络拓扑编辑器</h3>"
                                "<p>C++17 · Qt %1 · Graphics View Framework</p>"
                                "<p>同一套代码编译桌面版与 WebAssembly 版。</p>")
                     .arg(QString::fromLatin1(qVersion())));
    box->open();
}
