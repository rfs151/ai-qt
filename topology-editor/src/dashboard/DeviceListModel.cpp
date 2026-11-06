#include "dashboard/DeviceListModel.h"

void DeviceListModel::setRows(QVector<DeviceRow> rows)
{
    bool sameIds = rows.size() == m_rows.size();
    for (qsizetype i = 0; sameIds && i < rows.size(); ++i)
        sameIds = rows[i].id == m_rows[i].id;

    if (!sameIds) {
        beginResetModel();
        m_rows.swap(rows);
        endResetModel();
        return;
    }
    m_rows.swap(rows);
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1));
}

int DeviceListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const DeviceRow &r = m_rows[index.row()];
    switch (role) {
    case IdRole:     return r.id;
    case NameRole:   return r.name;
    case TypeRole:   return r.type;
    case StatusRole: return r.status;
    case OnlineRole: return r.online;
    case CpuRole:    return r.cpu;
    case TempRole:   return r.temp;
    case InRole:     return r.inMbps;
    case OutRole:    return r.outMbps;
    case SparkRole:  return QVariant::fromValue(r.spark);
    }
    return {};
}

QHash<int, QByteArray> DeviceListModel::roleNames() const
{
    return {
        {IdRole, "deviceId"},   {NameRole, "name"},    {TypeRole, "typeName"},
        {StatusRole, "status"}, {OnlineRole, "online"}, {CpuRole, "cpu"},
        {TempRole, "temp"},     {InRole, "inMbps"},    {OutRole, "outMbps"},
        {SparkRole, "spark"},
    };
}
