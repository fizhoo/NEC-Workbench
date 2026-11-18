#include "ui/cards/CompactNumberDelegate.h"

#include "ui/cards/SymbolCells.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QLineEdit>
#include <QToolTip>

namespace necwb::ui {

auto compactNumberText(const QString& text) -> QString
{
    // Short values, including everything typed by hand, keep their exact spelling.
    if (text.size() <= 9) return text;
    bool numeric{};
    const auto value = text.trimmed().toDouble(&numeric);
    return numeric ? QString::number(value, 'g', 6) : text;
}

auto CompactNumberDelegate::displayText(const QVariant& value, const QLocale& locale) const
    -> QString
{
    if (value.typeId() == QMetaType::QString) return compactNumberText(value.toString());
    return QStyledItemDelegate::displayText(value, locale);
}

auto CompactNumberDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view,
    const QStyleOptionViewItem& option, const QModelIndex& index) -> bool
{
    if (event->type() == QEvent::ToolTip && index.data(Qt::ToolTipRole).toString().isEmpty()) {
        const auto exact = index.data(Qt::DisplayRole).toString();
        if (compactNumberText(exact) != exact) {
            QToolTip::showText(event->globalPos(), QObject::tr("Exact value: %1").arg(exact), view);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

auto CompactNumberDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
    const QModelIndex& index) const -> QWidget*
{
    auto* editor = QStyledItemDelegate::createEditor(parent, option, index);
    auto* line = qobject_cast<QLineEdit*>(editor);
    if (line != nullptr && cells_ != nullptr && cells_->takesSymbols(index)) cells_->offerSymbols(line);
    return editor;
}

void CompactNumberDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* line = qobject_cast<QLineEdit*>(editor);
    const auto expression = index.data(CellExpressionRole).toString();
    if (line != nullptr && !expression.isEmpty()) line->setText(expression);
    else QStyledItemDelegate::setEditorData(editor, index);
}

}
