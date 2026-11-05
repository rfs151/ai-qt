#include "io/TopologyIO.h"
#include "scene/EdgeItem.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace TopologyIO {

QByteArray toJson(const TopologyScene *scene)
{
    // 按 id 排序，保证导出结果稳定，便于 diff
    QList<NodeItem *> nodes = scene->nodes().values();
    std::sort(nodes.begin(), nodes.end(), [](auto *a, auto *b) { return a->id() < b->id(); });

    QJsonArray jn;
    for (const NodeItem *n : std::as_const(nodes)) {
        jn.append(QJsonObject{
            {"id", n->id()},
            {"name", n->name()},
            {"type", nodeTypeKey(n->nodeType())},
            {"status", statusKey(n->status())},
            {"ip", n->ip()},
            {"x", qRound(n->pos().x())},
            {"y", qRound(n->pos().y())},
        });
    }

    QJsonArray je;
    for (const EdgeItem *e : scene->edges())
        je.append(QJsonObject{{"from", e->source()->id()}, {"to", e->target()->id()}, {"load", e->load()}});

    return QJsonDocument(QJsonObject{{"format", "qt-topology"}, {"version", 1}, {"nodes", jn}, {"edges", je}})
        .toJson(QJsonDocument::Indented);
}

bool fromJson(TopologyScene *scene, const QByteArray &data, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
        *error = QStringLiteral("JSON 解析失败（偏移 %1）：%2").arg(pe.offset).arg(pe.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != QLatin1String("qt-topology")) {
        *error = QStringLiteral("不是拓扑文件（缺少 format: qt-topology）");
        return false;
    }

    // 先完整校验再修改场景，失败时场景保持不变
    const QJsonArray jn = root.value("nodes").toArray();
    const QJsonArray je = root.value("edges").toArray();
    QSet<int> ids;
    for (const QJsonValue &v : jn) {
        const int id = v.toObject().value("id").toInt(-1);
        if (id < 0 || ids.contains(id)) {
            *error = QStringLiteral("节点 id 无效或重复：%1").arg(id);
            return false;
        }
        ids.insert(id);
    }
    for (const QJsonValue &v : je) {
        const QJsonObject o = v.toObject();
        if (!ids.contains(o.value("from").toInt(-1)) || !ids.contains(o.value("to").toInt(-1))) {
            *error = QStringLiteral("连线引用了不存在的节点");
            return false;
        }
    }

    scene->clearTopology();
    for (const QJsonValue &v : jn) {
        const QJsonObject o = v.toObject();
        NodeItem *n = scene->createNode(o.value("id").toInt(), nodeTypeFromKey(o.value("type").toString()),
                                        o.value("name").toString(),
                                        QPointF(o.value("x").toDouble(), o.value("y").toDouble()));
        n->setIp(o.value("ip").toString());
        n->setStatus(statusFromKey(o.value("status").toString()));
    }
    for (const QJsonValue &v : je) {
        const QJsonObject o = v.toObject();
        NodeItem *a = scene->node(o.value("from").toInt());
        NodeItem *b = scene->node(o.value("to").toInt());
        if (a != b && !a->isConnectedTo(b))
            scene->createEdge(a, b, o.value("load").toInt(20));
    }
    return true;
}

void buildSampleCampus(TopologyScene *scene)
{
    scene->clearTopology();
    auto *rng = QRandomGenerator::global();
    int id = 1;
    auto make = [&](NodeType t, const QString &name, qreal x, qreal y, const QString &ip) {
        NodeItem *n = scene->createNode(id++, t, name, QPointF(x, y));
        n->setIp(ip);
        return n;
    };

    NodeItem *internet = make(NodeType::Cloud, QStringLiteral("互联网"), 0, -480, QString());
    NodeItem *fw = make(NodeType::Firewall, QStringLiteral("出口防火墙"), 0, -360, QStringLiteral("10.0.0.1"));
    scene->createEdge(internet, fw, 45);

    NodeItem *core1 = make(NodeType::Router, QStringLiteral("核心-A"), -160, -240, QStringLiteral("10.0.1.1"));
    NodeItem *core2 = make(NodeType::Router, QStringLiteral("核心-B"), 160, -240, QStringLiteral("10.0.1.2"));
    scene->createEdge(fw, core1, 50);
    scene->createEdge(fw, core2, 40);
    scene->createEdge(core1, core2, 30);

    // 数据中心
    NodeItem *dcSw = make(NodeType::Switch, QStringLiteral("机房交换机"), 520, -240, QStringLiteral("10.0.9.1"));
    scene->createEdge(core2, dcSw, 65);
    const char *const servers[] = {"Web 服务器", "数据库主", "数据库备", "文件服务器"};
    for (int i = 0; i < 4; ++i) {
        NodeItem *s = make(NodeType::Server, QString::fromUtf8(servers[i]), 400 + i * 80, -100,
                           QStringLiteral("10.0.9.%1").arg(10 + i));
        scene->createEdge(dcSw, s, 20 + rng->bounded(60));
    }

    // 三栋楼：汇聚 + 接入 + 终端
    const char *const buildings[] = {"研发楼", "行政楼", "生产车间"};
    for (int b = 0; b < 3; ++b) {
        const qreal bx = -480 + b * 360;
        NodeItem *agg = make(NodeType::Switch, QStringLiteral("%1汇聚").arg(QString::fromUtf8(buildings[b])), bx, -80,
                             QStringLiteral("10.%1.0.1").arg(b + 1));
        scene->createEdge(core1, agg, 30 + rng->bounded(40));
        scene->createEdge(core2, agg, 10 + rng->bounded(30));
        for (int f = 0; f < 3; ++f) {
            const qreal fx = bx - 120 + f * 120;
            NodeItem *acc = make(NodeType::Switch, QStringLiteral("%1F 接入").arg(f + 1), fx, 60,
                                 QStringLiteral("10.%1.%2.1").arg(b + 1).arg(f + 1));
            scene->createEdge(agg, acc, 10 + rng->bounded(50));
            for (int w = 0; w < 2; ++w) {
                NodeItem *pc = make(NodeType::Workstation, QStringLiteral("PC-%1%2%3").arg(b + 1).arg(f + 1).arg(w + 1),
                                    fx - 30 + w * 60, 180 + w * 20,
                                    QStringLiteral("10.%1.%2.%3").arg(b + 1).arg(f + 1).arg(100 + w));
                scene->createEdge(acc, pc, rng->bounded(40));
            }
        }
    }
}

void buildStressGrid(TopologyScene *scene, int count)
{
    scene->clearTopology();
    auto *rng = QRandomGenerator::global();
    const int cols = int(std::ceil(std::sqrt(double(count))));
    constexpr int kSpacing = 120;

    QVector<NodeItem *> grid(count, nullptr);
    for (int i = 0; i < count; ++i) {
        const int r = i / cols, c = i % cols;
        const NodeType t = (r % 10 == 0 && c % 10 == 0) ? NodeType::Router
                           : (c % 5 == 0)                ? NodeType::Switch
                                                         : NodeType(2 + rng->bounded(3));
        grid[i] = scene->createNode(i + 1, t, QStringLiteral("N%1").arg(i + 1),
                                    QPointF(c * kSpacing, r * kSpacing));
        grid[i]->setIp(QStringLiteral("10.%1.%2.%3").arg(i / 65536 % 256).arg(i / 256 % 256).arg(i % 256));
    }
    for (int i = 0; i < count; ++i) {
        const int r = i / cols, c = i % cols;
        if (c + 1 < cols && i + 1 < count)
            scene->createEdge(grid[i], grid[i + 1], rng->bounded(100));
        if (i + cols < count && rng->bounded(2) == 0)
            scene->createEdge(grid[i], grid[i + cols], rng->bounded(100));
        Q_UNUSED(r);
    }
}

} // namespace TopologyIO
