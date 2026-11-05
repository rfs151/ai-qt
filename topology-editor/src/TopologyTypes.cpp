#include "TopologyTypes.h"

namespace {
const char *const kTypeKeys[kNodeTypeCount] = {"router", "switch", "server", "firewall", "workstation", "cloud"};
const char *const kStatusKeys[kNodeStatusCount] = {"normal", "warning", "alarm", "offline"};
} // namespace

QString nodeTypeName(NodeType t)
{
    switch (t) {
    case NodeType::Router:      return QStringLiteral("路由器");
    case NodeType::Switch:      return QStringLiteral("交换机");
    case NodeType::Server:      return QStringLiteral("服务器");
    case NodeType::Firewall:    return QStringLiteral("防火墙");
    case NodeType::Workstation: return QStringLiteral("工作站");
    case NodeType::Cloud:       return QStringLiteral("云服务");
    }
    return {};
}

QString nodeTypeKey(NodeType t) { return QString::fromLatin1(kTypeKeys[int(t)]); }

NodeType nodeTypeFromKey(const QString &key)
{
    for (int i = 0; i < kNodeTypeCount; ++i)
        if (key == QLatin1String(kTypeKeys[i]))
            return NodeType(i);
    return NodeType::Server;
}

QStringList nodeTypeNames()
{
    QStringList l;
    for (int i = 0; i < kNodeTypeCount; ++i)
        l << nodeTypeName(NodeType(i));
    return l;
}

QString statusName(NodeStatus s)
{
    switch (s) {
    case NodeStatus::Normal:  return QStringLiteral("正常");
    case NodeStatus::Warning: return QStringLiteral("警告");
    case NodeStatus::Alarm:   return QStringLiteral("告警");
    case NodeStatus::Offline: return QStringLiteral("离线");
    }
    return {};
}

QString statusKey(NodeStatus s) { return QString::fromLatin1(kStatusKeys[int(s)]); }

NodeStatus statusFromKey(const QString &key)
{
    for (int i = 0; i < kNodeStatusCount; ++i)
        if (key == QLatin1String(kStatusKeys[i]))
            return NodeStatus(i);
    return NodeStatus::Normal;
}

QStringList statusNames()
{
    QStringList l;
    for (int i = 0; i < kNodeStatusCount; ++i)
        l << statusName(NodeStatus(i));
    return l;
}

QColor statusColor(NodeStatus s)
{
    switch (s) {
    case NodeStatus::Normal:  return QColor(0x2e, 0xa0, 0x43);
    case NodeStatus::Warning: return QColor(0xe0, 0x9b, 0x10);
    case NodeStatus::Alarm:   return QColor(0xd9, 0x30, 0x25);
    case NodeStatus::Offline: return QColor(0x8a, 0x8f, 0x98);
    }
    return Qt::gray;
}
