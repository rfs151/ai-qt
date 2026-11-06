#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

struct DeviceRow
{
    int id = 0;
    QString name;
    QString type;
    int status = 0;   // NodeStatus
    bool online = false;
    double cpu = 0;
    double temp = 0;
    double inMbps = 0;
    double outMbps = 0;
    QList<qreal> spark;   // 最近一分钟的 CPU，给卡片上的迷你曲线
};

// 大屏设备卡片的数据源。设备集合不变时只发 dataChanged，GridView 不会重建委托
class DeviceListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("由 Backend.devices 提供")
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        StatusRole,
        OnlineRole,
        CpuRole,
        TempRole,
        InRole,
        OutRole,
        SparkRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setRows(QVector<DeviceRow> rows);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QVector<DeviceRow> m_rows;
};
