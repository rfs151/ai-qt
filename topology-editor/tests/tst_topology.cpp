#include "MainWindow.h"
#include "commands/Commands.h"
#include "io/TopologyIO.h"
#include "model/AlarmLogModel.h"
#include "scene/EdgeItem.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"

#include <QAction>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QtTest>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QUndoStack>

class TestTopology : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(FONT_PATH));
        if (id >= 0) {
            QFont f(QFontDatabase::applicationFontFamilies(id).first());
            f.setPixelSize(13);
            QApplication::setFont(f);
        }
    }

    void addNodeUndoRedo()
    {
        QUndoStack stack;
        TopologyScene scene(&stack);
        stack.push(new AddNodeCommand(&scene, new NodeItem(1, NodeType::Server, "s1")));
        QCOMPARE(scene.nodes().size(), 1);
        stack.undo();
        QCOMPARE(scene.nodes().size(), 0);
        stack.redo();
        QCOMPARE(scene.nodes().size(), 1);

        // 撤销后再 push 新命令：被丢弃的 AddNodeCommand 负责释放节点（ASan/valgrind 下不应泄漏或重复释放）
        stack.undo();
        stack.push(new AddNodeCommand(&scene, new NodeItem(2, NodeType::Router, "r2")));
        QCOMPARE(scene.nodes().size(), 1);
        QVERIFY(scene.node(2));
    }

    void deleteRestoresEdges()
    {
        QUndoStack stack;
        TopologyScene scene(&stack);
        NodeItem *a = scene.createNode(1, NodeType::Router, "a", {0, 0});
        NodeItem *b = scene.createNode(2, NodeType::Switch, "b", {100, 0});
        NodeItem *c = scene.createNode(3, NodeType::Server, "c", {200, 0});
        scene.createEdge(a, b, 10);
        scene.createEdge(b, c, 20);

        stack.push(new DeleteItemsCommand(&scene, {b}, {}));
        QCOMPARE(scene.nodes().size(), 2);
        QCOMPARE(scene.edges().size(), 0);
        QCOMPARE(a->edges().size(), 0);
        QCOMPARE(c->edges().size(), 0);

        stack.undo();
        QCOMPARE(scene.nodes().size(), 3);
        QCOMPARE(scene.edges().size(), 2);
        QCOMPARE(b->edges().size(), 2);
        QVERIFY(a->isConnectedTo(b));

        stack.redo();
        stack.clear();   // 已执行的删除命令释放 b 和两条连线
        QCOMPARE(scene.nodes().size(), 2);
    }

    void edgeFollowsNode()
    {
        QUndoStack stack;
        TopologyScene scene(&stack);
        NodeItem *a = scene.createNode(1, NodeType::Router, "a", {0, 0});
        NodeItem *b = scene.createNode(2, NodeType::Switch, "b", {100, 0});
        EdgeItem *e = scene.createEdge(a, b, 10);
        b->setPos(300, 200);
        QVERIFY(e->boundingRect().contains(QPointF(300, 200)));
    }

    void arrowMovesMerge()
    {
        QUndoStack stack;
        TopologyScene scene(&stack);
        NodeItem *a = scene.createNode(1, NodeType::Router, "a", {0, 0});
        for (int i = 1; i <= 5; ++i) {
            auto *cmd = new MoveNodesCommand({a}, {a->pos()}, {QPointF(i * 20, 0)}, "move");
            cmd->setMergeable(true);
            stack.push(cmd);
        }
        QCOMPARE(stack.count(), 1);
        QCOMPARE(a->pos(), QPointF(100, 0));
        stack.undo();
        QCOMPARE(a->pos(), QPointF(0, 0));
    }

    void propertyCommand()
    {
        QUndoStack stack;
        TopologyScene scene(&stack);
        NodeItem *a = scene.createNode(1, NodeType::Router, "a", {0, 0});
        stack.push(new SetNodePropertyCommand(a, SetNodePropertyCommand::Property::Status, int(NodeStatus::Alarm)));
        QCOMPARE(scene.statusCount(NodeStatus::Alarm), 1);
        stack.undo();
        QCOMPARE(scene.statusCount(NodeStatus::Alarm), 0);
        QCOMPARE(scene.statusCount(NodeStatus::Normal), 1);
    }

    void jsonRoundTrip()
    {
        QUndoStack stack;
        TopologyScene src(&stack);
        TopologyIO::buildSampleCampus(&src);
        src.node(3)->setStatus(NodeStatus::Warning);
        const QByteArray json = TopologyIO::toJson(&src);

        TopologyScene dst(&stack);
        QString error;
        QVERIFY2(TopologyIO::fromJson(&dst, json, &error), qPrintable(error));
        QCOMPARE(dst.nodes().size(), src.nodes().size());
        QCOMPARE(dst.edges().size(), src.edges().size());
        QCOMPARE(dst.node(3)->status(), NodeStatus::Warning);
        QCOMPARE(dst.node(3)->name(), src.node(3)->name());
        QCOMPARE(dst.node(3)->pos(), src.node(3)->pos());
    }

    void jsonRejectsBadInput()
    {
        QUndoStack stack;
        TopologyScene scene(&stack);
        TopologyIO::buildSampleCampus(&scene);
        const auto before = scene.nodes().size();
        QString error;
        QVERIFY(!TopologyIO::fromJson(&scene, "{ not json", &error));
        QVERIFY(!TopologyIO::fromJson(&scene, R"({"format":"qt-topology","nodes":[{"id":1}],"edges":[{"from":1,"to":9}]})", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(scene.nodes().size(), before);   // 失败时场景不变
    }

    void stressGrid()
    {
        QUndoStack stack;
        TopologyScene scene(&stack);
        QElapsedTimer t;
        t.start();
        TopologyIO::buildStressGrid(&scene, 10000);
        qInfo("build 10000 nodes: %lld ms, edges=%lld", t.elapsed(), qlonglong(scene.edges().size()));
        QCOMPARE(scene.nodes().size(), 10000);
        QVERIFY(scene.edges().size() > 12000);
    }

    void alarmLogCap()
    {
        AlarmLogModel model;
        QVector<AlarmEvent> batch;
        for (int i = 0; i < 1500; ++i)
            batch.append({QDateTime::currentDateTime(), i, QString::number(i), NodeStatus::Alarm, "x"});
        model.append(batch);
        model.append(batch);
        QCOMPARE(model.rowCount(), AlarmLogModel::kMaxRows);
        // 最新的在最前
        QCOMPARE(model.index(0, AlarmLogModel::ColNode).data().toString(), QStringLiteral("1499"));
    }

    void screenshots()
    {
        MainWindow w;
        w.resize(1400, 860);
        w.show();
        QTest::qWait(400);
        QVERIFY(w.grab().save(QStringLiteral(SCREENSHOT_DIR "/screenshot-sample.png")));

        // 设备面板的仪表盘页和历史查询页
        QTabWidget *tabs = nullptr;
        for (QTabWidget *t : w.findChildren<QTabWidget *>())
            if (t->count() > 0 && t->tabText(0) == QStringLiteral("实时曲线"))
                tabs = t;
        QVERIFY(tabs);
        QTest::qWait(3000);
        QVERIFY(w.grab().save(QStringLiteral(SCREENSHOT_DIR "/screenshot-curves.png")));
        tabs->setCurrentIndex(1);
        QTest::qWait(1200);
        QVERIFY(w.grab().save(QStringLiteral(SCREENSHOT_DIR "/screenshot-gauges.png")));
        tabs->setCurrentIndex(tabs->count() - 1);
        for (QPushButton *b : w.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("查询"))
                b->click();
        QTest::qWait(800);
        QVERIFY(w.grab().save(QStringLiteral(SCREENSHOT_DIR "/screenshot-history.png")));
        // 切到 Modbus 协议重连，看寄存器页
        QPushButton *connectBtn = nullptr;
        for (QPushButton *b : w.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("断开"))
                connectBtn = b;
        QVERIFY(connectBtn);
        connectBtn->click();
        QTest::qWait(300);
        for (QComboBox *c : w.findChildren<QComboBox *>())
            if (c->count() == 2 && c->itemText(1) == QStringLiteral("Modbus"))
                c->setCurrentIndex(1);
        connectBtn->click();
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == QStringLiteral("寄存器"))
                tabs->setCurrentIndex(i);
        QTest::qWait(2500);
        QVERIFY(w.grab().save(QStringLiteral(SCREENSHOT_DIR "/screenshot-modbus.png")));
        tabs->setCurrentIndex(0);

        // 开启告警模拟一段时间后再截一张
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == QStringLiteral("告警模拟"))
                a->trigger();
        QTest::qWait(2500);
        QVERIFY(w.grab().save(QStringLiteral(SCREENSHOT_DIR "/screenshot-alarm.png")));

        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == QStringLiteral("10000 个节点"))
                a->trigger();
        QTest::qWait(800);
        QVERIFY(w.grab().save(QStringLiteral(SCREENSHOT_DIR "/screenshot-stress.png")));
    }
};

QTEST_MAIN(TestTopology)
#include "tst_topology.moc"
