#pragma once

#include <QList>
#include <QStyledItemDelegate>

#include <utility>

namespace necwb::ui {

// A cell written as an expression holds it here; its text is the value.
constexpr auto CellExpressionRole = Qt::UserRole + 40;

// Draws long numeric cell text at 6 significant figures so card tables stay narrow.
// Only the display changes: editors and the tooltip use the exact cell text, so the
// deck is never rounded. An expression cell edits as written, and typing offers the symbols.
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

    // Names and values, offered as "len = 5.1".
    void setSymbols(QList<std::pair<QString, double>> symbols);

private:
    QList<std::pair<QString, double>> symbols_;
};

// Text shown for a card cell: numbers longer than 9 characters at 6 significant figures,
// everything else unchanged.
[[nodiscard]] auto compactNumberText(const QString& text) -> QString;

}
