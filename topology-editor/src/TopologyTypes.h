#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

enum class NodeType { Router, Switch, Server, Firewall, Workstation, Cloud };
enum class NodeStatus { Normal, Warning, Alarm, Offline };

constexpr int kNodeTypeCount = 6;
constexpr int kNodeStatusCount = 4;

QString nodeTypeName(NodeType t);
QString nodeTypeKey(NodeType t);            // JSON 中使用的英文 key
NodeType nodeTypeFromKey(const QString &key);
QStringList nodeTypeNames();

QString statusName(NodeStatus s);
QString statusKey(NodeStatus s);
NodeStatus statusFromKey(const QString &key);
QStringList statusNames();
QColor statusColor(NodeStatus s);
