#include "model/PropertyDelegate.h"
#include "model/PropertyModel.h"
#include "TopologyTypes.h"

#include <QComboBox>
#include <QLineEdit>
#include <QPainter>
#include <QRegularExpressionValidator>
#include <QSpinBox>

using Editor = PropertyModel::Editor;

namespace {
Editor editorOf(const QModelIndex &index)
{
    return Editor(index.data(PropertyModel::EditorRole).toInt());
}
} // namespace

QWidget *PropertyDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                        const QModelIndex &index) const
{
    switch (editorOf(index)) {
    case Editor::Text:
        return new QLineEdit(parent);
    case Editor::Ip: {
        auto *e = new QLineEdit(parent);
        static const QRegularExpression re(
            QStringLiteral(R"(^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$)"));
        e->setValidator(new QRegularExpressionValidator(re, e));
        e->setPlaceholderText(QStringLiteral("例如 10.0.0.1"));
        return e;
    }
    case Editor::TypeCombo:
    case Editor::StatusCombo: {
        auto *c = new QComboBox(parent);
        if (editorOf(index) == Editor::TypeCombo) {
            c->addItems(nodeTypeNames());
        } else {
            for (int i = 0; i < kNodeStatusCount; ++i) {
                QPixmap pm(10, 10);
                pm.fill(statusColor(NodeStatus(i)));
                c->addItem(QIcon(pm), statusName(NodeStatus(i)));
            }
        }
        // 选中即提交，不用再点别处
        connect(c, &QComboBox::activated, this, [this, c] {
            emit const_cast<PropertyDelegate *>(this)->commitData(c);
            emit const_cast<PropertyDelegate *>(this)->closeEditor(c);
        });
        return c;
    }
    case Editor::Coordinate: {
        auto *s = new QSpinBox(parent);
        s->setRange(-30000, 30000);
        s->setSingleStep(20);
        return s;
    }
    case Editor::None:
        break;
    }
    return QStyledItemDelegate::createEditor(parent, option, index);
}

void PropertyDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    const QVariant v = index.data(Qt::EditRole);
    if (auto *c = qobject_cast<QComboBox *>(editor))
        c->setCurrentIndex(v.toInt());
    else if (auto *s = qobject_cast<QSpinBox *>(editor))
        s->setValue(qRound(v.toDouble()));
    else if (auto *e = qobject_cast<QLineEdit *>(editor))
        e->setText(v.toString());
    else
        QStyledItemDelegate::setEditorData(editor, index);
}

void PropertyDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    if (auto *c = qobject_cast<QComboBox *>(editor))
        model->setData(index, c->currentIndex());
    else if (auto *s = qobject_cast<QSpinBox *>(editor))
        model->setData(index, double(s->value()));
    else if (auto *e = qobject_cast<QLineEdit *>(editor)) {
        if (e->hasAcceptableInput())
            model->setData(index, e->text().trimmed());
    } else
        QStyledItemDelegate::setModelData(editor, model, index);
}

void PropertyDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    // 可编辑的值列用浅色底提示
    if (index.column() == 1 && (index.flags() & Qt::ItemIsEditable) && !(opt.state & QStyle::State_Selected))
        painter->fillRect(opt.rect, QColor(0xf3, 0xf7, 0xfd));
    QStyledItemDelegate::paint(painter, opt, index);
}
