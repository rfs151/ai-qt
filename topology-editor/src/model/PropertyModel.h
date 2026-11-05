#pragma once

#include <QAbstractTableModel>
#include <QPointer>

class NodeItem;
class QUndoStack;

// 选中节点的属性表。编辑结果不直接写节点，而是生成撤销命令。
class PropertyModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Row { RowId, RowName, RowType, RowIp, RowStatus, RowX, RowY, RowLinks, RowCount };
    enum class Editor { None, Text, Ip, TypeCombo, StatusCombo, Coordinate };
    static constexpr int EditorRole = Qt::UserRole + 1;

    explicit PropertyModel(QUndoStack *undo, QObject *parent = nullptr);

    void setNode(NodeItem *node);
    NodeItem *node() const { return m_node; }
    void refresh();

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QUndoStack *m_undo;
    NodeItem *m_node = nullptr;
};
