#include "model/AlarmLogModel.h"

#include <QBrush>

void AlarmLogModel::append(const QVector<AlarmEvent> &events)
{
    if (events.isEmpty())
        return;

    // 超出上限时先删尾部旧数据
    const int overflow = int(m_rows.size() + events.size()) - kMaxRows;
    if (overflow > 0) {
        const int removeCount = std::min<int>(overflow, int(m_rows.size()));
        if (removeCount > 0) {
            beginRemoveRows({}, int(m_rows.size()) - removeCount, int(m_rows.size()) - 1);
            m_rows.resize(m_rows.size() - removeCount);
            endRemoveRows();
        }
    }

    const int n = std::min<int>(int(events.size()), kMaxRows);
    beginInsertRows({}, 0, n - 1);
    QVector<AlarmEvent> fresh;
    fresh.reserve(n + m_rows.size());
    for (int i = int(events.size()) - 1; i >= int(events.size()) - n; --i)
        fresh.append(events[i]);
    fresh += m_rows;
    m_rows.swap(fresh);
    endInsertRows();
}

void AlarmLogModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

int AlarmLogModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }
int AlarmLogModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColCount; }

QVariant AlarmLogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColTime:  return QStringLiteral("时间");
    case ColNode:  return QStringLiteral("节点");
    case ColLevel: return QStringLiteral("级别");
    case ColText:  return QStringLiteral("描述");
    }
    return {};
}

QHash<int, QByteArray> AlarmLogModel::roleNames() const
{
    QHash<int, QByteArray> r = QAbstractTableModel::roleNames();
    r.insert(NodeIdRole, "nodeId");
    r.insert(LevelRole, "level");
    r.insert(TimeTextRole, "timeText");
    r.insert(NodeNameRole, "nodeName");
    r.insert(LevelNameRole, "levelName");
    r.insert(TextRole, "text");
    return r;
}

QVariant AlarmLogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const AlarmEvent &e = m_rows[index.row()];

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColTime:  return e.time.toString(QStringLiteral("HH:mm:ss.zzz"));
        case ColNode:  return e.nodeName;
        case ColLevel: return statusName(e.level);
        case ColText:  return e.text;
        }
        break;
    case Qt::ForegroundRole:
        if (index.column() == ColLevel)
            return QBrush(statusColor(e.level));
        break;
    case NodeIdRole:
        return e.nodeId;
    case LevelRole:
        return int(e.level);
    case TimeTextRole:
        return e.time.toString(QStringLiteral("HH:mm:ss"));
    case NodeNameRole:
        return e.nodeName;
    case LevelNameRole:
        return statusName(e.level);
    case TextRole:
        return e.text;
    }
    return {};
}

// ---------- AlarmFilterProxy ----------

void AlarmFilterProxy::setMinimumLevel(int level)
{
    m_minLevel = level;
    invalidateRowsFilter();
}

void AlarmFilterProxy::setKeyword(const QString &text)
{
    m_keyword = text.trimmed();
    invalidateRowsFilter();
}

bool AlarmFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const int level = idx.data(AlarmLogModel::LevelRole).toInt();

    if (m_minLevel >= 0) {
        // 级别顺序：正常 < 警告 < 告警；离线视为与告警同级
        auto rank = [](int l) { return l == int(NodeStatus::Offline) ? int(NodeStatus::Alarm) : l; };
        if (rank(level) < rank(m_minLevel))
            return false;
    }
    if (m_keyword.isEmpty())
        return true;
    for (int c = 0; c < AlarmLogModel::ColCount; ++c) {
        const QString s = sourceModel()->index(sourceRow, c, sourceParent).data().toString();
        if (s.contains(m_keyword, Qt::CaseInsensitive))
            return true;
    }
    return false;
}
