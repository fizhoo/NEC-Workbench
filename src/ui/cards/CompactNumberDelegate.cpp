#include "ui/cards/CompactNumberDelegate.h"

#include <QAbstractItemView>
#include <QHelpEvent>
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

}
