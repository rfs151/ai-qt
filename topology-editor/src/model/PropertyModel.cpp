#include "model/PropertyModel.h"
#include "commands/Commands.h"
#include "scene/NodeItem.h"

#include <QBrush>
#include <QUndoStack>

using Prop = SetNodePropertyCommand::Property;

namespace {
const char *const kRowNames[PropertyModel::RowCount] = {"ID", "名称", "类型", "IP 地址", "状态", "X", "Y", "连接数"};

bool rowToProp(int row, Prop *out)
{
    switch (row) {
    case PropertyModel::RowName:   *out = Prop::Name; return true;
    case PropertyModel::RowType:   *out = Prop::Type; return true;
    case PropertyModel::RowIp:     *out = Prop::Ip; return true;
    case PropertyModel::RowStatus: *out = Prop::Status; return true;
    case PropertyModel::RowX:      *out = Prop::X; return true;
    case PropertyModel::RowY:      *out = Prop::Y; return true;
    default: return false;
    }
}
} // namespace

PropertyModel::PropertyModel(QUndoStack *undo, QObject *parent)
    : QAbstractTableModel(parent), m_undo(undo)
{
    // 撤销/重做后属性值可能变化
    connect(m_undo, &QUndoStack::indexChanged, this, &PropertyModel::refresh);
}

void PropertyModel::setNode(NodeItem *node)
{
    if (m_node == node)
        return;
    beginResetModel();
    m_node = node;
    endResetModel();
}

void PropertyModel::refresh()
{
    if (m_node)
        emit dataChanged(index(0, 1), index(RowCount - 1, 1));
}

int PropertyModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_node ? 0 : RowCount;
}

int PropertyModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 2; }

QVariant PropertyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return section == 0 ? QStringLiteral("属性") : QStringLiteral("值");
    return {};
}

Qt::ItemFlags PropertyModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    Prop p;
    if (index.column() == 1 && rowToProp(index.row(), &p))
        f |= Qt::ItemIsEditable;
    return f;
}

QVariant PropertyModel::data(const QModelIndex &index, int role) const
{
    if (!m_node || !index.isValid())
        return {};
    const int row = index.row();

    if (index.column() == 0) {
        if (role == Qt::DisplayRole)
            return QString::fromUtf8(kRowNames[row]);
        if (role == Qt::ForegroundRole)
            return QBrush(QColor(0x55, 0x5f, 0x6d));
        return {};
    }

    if (role == EditorRole) {
        switch (row) {
        case RowName:   return int(Editor::Text);
        case RowIp:     return int(Editor::Ip);
        case RowType:   return int(Editor::TypeCombo);
        case RowStatus: return int(Editor::StatusCombo);
        case RowX:
        case RowY:      return int(Editor::Coordinate);
        default:        return int(Editor::None);
        }
    }

    if (role == Qt::DecorationRole && row == RowStatus)
        return statusColor(m_node->status());

    if (role == Qt::DisplayRole) {
        switch (row) {
        case RowId:     return m_node->id();
        case RowName:   return m_node->name();
        case RowType:   return nodeTypeName(m_node->nodeType());
        case RowIp:     return m_node->ip();
        case RowStatus: return statusName(m_node->status());
        case RowX:      return qRound(m_node->pos().x());
        case RowY:      return qRound(m_node->pos().y());
        case RowLinks:  return m_node->edges().size();
        }
    }

    if (role == Qt::EditRole) {
        Prop p;
        if (rowToProp(row, &p))
            return SetNodePropertyCommand::read(m_node, p);
    }
    return {};
}

bool PropertyModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Prop p;
    if (!m_node || role != Qt::EditRole || !rowToProp(index.row(), &p))
        return false;
    if (SetNodePropertyCommand::read(m_node, p) == value)
        return false;
    m_undo->push(new SetNodePropertyCommand(m_node, p, value));
    return true;
}
