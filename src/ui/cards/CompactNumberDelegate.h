#pragma once

#include <QStyledItemDelegate>

namespace necwb::ui {

class SymbolCells;

// Draws long numeric cell text at 6 significant figures so card tables stay narrow.
// Only the display changes: editors and the tooltip use the exact cell text, so the
// deck is never rounded. An expression cell edits as written, and a cell that takes symbols
// offers them as you type (SymbolCells).
class CompactNumberDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] auto displayText(const QVariant& value, const QLocale& locale) const
        -> QString override;
    auto helpEvent(QHelpEvent* event, QAbstractItemView* view,
        const QStyleOptionViewItem& option, const QModelIndex& index) -> bool override;
    auto createEditor(QWidget* parent, const QStyleOptionViewItem& option,
        const QModelIndex& index) const -> QWidget* override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;

    void setSymbolCells(const SymbolCells* cells) { cells_ = cells; }

private:
    const SymbolCells* cells_{};
};

// Text shown for a card cell: numbers longer than 9 characters at 6 significant figures,
// everything else unchanged.
[[nodiscard]] auto compactNumberText(const QString& text) -> QString;

}
