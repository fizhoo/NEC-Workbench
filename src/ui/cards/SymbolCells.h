#pragma once

#include "nec/NecSymbolResolver.h"
#include "ui/cards/SymbolPrompt.h"

#include <QList>
#include <QObject>
#include <QPoint>
#include <QString>

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QLineEdit;
class QModelIndex;
class QTableWidget;
class QTableWidgetItem;

namespace necwb::ui {

// A cell written as an expression holds it here; its text is the value.
constexpr auto CellExpressionRole = Qt::UserRole + 40;

// The deck field a card-table cell edits.
struct CellField {
    std::size_t sourceLine{};
    std::size_t fieldIndex{};
    QString label;
};

// Symbols in a card table, the same in every table: a number cell takes a name or an
// expression such as len/2, offering the model's symbols for the word being typed; an
// expression cell shows its value, styled, and edits as written; right-click makes a variable
// or, on a cell written as an expression, replaces it with its value.
class SymbolCells final : public QObject {
    Q_OBJECT

public:
    // `fieldOf` gives the field of a cell that takes symbols, and none for any other cell.
    using FieldOf = std::function<std::optional<CellField>(const QTableWidgetItem&)>;
    SymbolCells(QTableWidget* table, FieldOf fieldOf);

    void setSymbols(const std::vector<nec::SymbolDefinition>& symbols);
    // Whether the cell at `index` takes symbols; the delegate asks when it opens an editor.
    [[nodiscard]] auto takesSymbols(const QModelIndex& index) const -> bool;
    // Offers the symbols in a cell's editor, for the word at the cursor.
    void offerSymbols(QLineEdit* editor) const;
    // A changed cell whose text is not a plain number, or that was an expression, goes to the
    // deck as written through fieldExpressionEntered, and `restore` puts the cell back until
    // the deck changes. False for an ordinary number edit, which the table commits itself.
    auto takeExpressionEdit(QTableWidgetItem* item, std::function<void()> restore) -> bool;

    // Shows an expression cell: its value, ƒx, the expression in the tooltip and the editor.
    static void showExpression(QTableWidgetItem* item, QTableWidget* table,
        const QString& expression, const QString& value);
    [[nodiscard]] static auto usesExpression(const QTableWidgetItem& item) -> bool;

signals:
    void fieldParameterizationRequested(std::size_t sourceLine,
        std::size_t fieldIndex, QString fieldLabel);
    void fieldDetachmentRequested(std::size_t sourceLine,
        std::size_t fieldIndex, QString fieldLabel);
    // `place` is the cell and the table on screen, where a new name's value is asked.
    void fieldExpressionEntered(std::size_t sourceLine, std::size_t fieldIndex,
        QString fieldLabel, QString expression, PromptPlace place);

private:
    void showMenu(QPoint position);

    QTableWidget* table_{};
    FieldOf fieldOf_;
    QList<std::pair<QString, double>> symbols_;
};

}
