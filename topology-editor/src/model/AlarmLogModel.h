#pragma once

#include "TopologyTypes.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <QVector>

struct AlarmEvent
{
    QDateTime time;
    int nodeId;
    QString nodeName;
    NodeStatus level;
    QString text;
};

// 告警日志，环形保留最近 kMaxRows 条
class AlarmLogModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColTime, ColNode, ColLevel, ColText, ColCount };
    static constexpr int NodeIdRole = Qt::UserRole + 1;
    static constexpr int LevelRole = Qt::UserRole + 2;
    static constexpr int kMaxRows = 2000;

    using QAbstractTableModel::QAbstractTableModel;

    void append(const QVector<AlarmEvent> &events);   // 批量追加，一次 beginInsertRows
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<AlarmEvent> m_rows;   // 最新的在前
};

// 按"最低级别 + 关键字"过滤
class AlarmFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setMinimumLevel(int level);   // -1 表示全部
    void setKeyword(const QString &text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_minLevel = -1;
    QString m_keyword;
};
