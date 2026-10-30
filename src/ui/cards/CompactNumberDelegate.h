#pragma once

#include <QStyledItemDelegate>

namespace necwb::ui {

// Draws long numeric cell text at 6 significant figures so card tables stay narrow.
// Only the display changes: editors and the tooltip use the exact cell text, so the
// deck is never rounded.
class CompactNumberDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] auto displayText(const QVariant& value, const QLocale& locale) const
        -> QString override;
    auto helpEvent(QHelpEvent* event, QAbstractItemView* view,
        const QStyleOptionViewItem& option, const QModelIndex& index) -> bool override;
};

// Text shown for a card cell: numbers longer than 9 characters at 6 significant figures,
// everything else unchanged.
[[nodiscard]] auto compactNumberText(const QString& text) -> QString;

}
