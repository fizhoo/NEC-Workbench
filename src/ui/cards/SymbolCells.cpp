#include "ui/cards/SymbolCells.h"

#include "ui/ParameterFieldStyle.h"
#include "ui/cards/CardFields.h"
#include "ui/cards/VariableMenu.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTimer>

namespace necwb::ui {
namespace {

auto nameCharacter(QChar character) -> bool
{
    return character.isLetterOrNumber() || character == QLatin1Char('_');
}

// The name around the cursor: where it starts and how long it is.
auto wordAt(const QString& text, int cursor) -> std::pair<int, int>
{
    auto begin = cursor;
    while (begin > 0 && nameCharacter(text[begin - 1])) --begin;
    auto end = cursor;
    while (end < text.size() && nameCharacter(text[end])) ++end;
    // A number such as 1e3 is not a name.
    if (begin < end && text[begin].isDigit()) return {cursor, 0};
    return {begin, end - begin};
}

}

SymbolCells::SymbolCells(QTableWidget* table, FieldOf fieldOf)
    : QObject(table), table_(table), fieldOf_(std::move(fieldOf))
{
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableWidget::customContextMenuRequested, this, &SymbolCells::showMenu);
}

void SymbolCells::setSymbols(const std::vector<nec::SymbolDefinition>& symbols)
{
    symbols_.clear();
    for (const auto& symbol : symbols)
        symbols_.append({QString::fromStdString(symbol.name), symbol.value});
}

auto SymbolCells::takesSymbols(const QModelIndex& index) const -> bool
{
    const auto* item = table_->item(index.row(), index.column());
    return item != nullptr && fieldOf_(*item).has_value();
}

void SymbolCells::offerSymbols(QLineEdit* editor) const
{
    if (symbols_.isEmpty()) return;
    // The list shows each value; choosing one types its name over the word being typed.
    constexpr auto NameRole = Qt::UserRole;
    auto* names = new QStandardItemModel(editor);
    for (const auto& [name, value] : symbols_) {
        auto* item = new QStandardItem(QObject::tr("%1 = %2").arg(name).arg(value, 0, 'g', 12));
        item->setData(name, NameRole);
        names->appendRow(item);
    }
    auto* completer = new QCompleter(names, editor);
    completer->setWidget(editor);
    completer->setCompletionRole(NameRole);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    connect(editor, &QLineEdit::textEdited, completer, [editor, completer] {
        const auto [begin, length] = wordAt(editor->text(), editor->cursorPosition());
        if (length == 0) {
            completer->popup()->hide();
            return;
        }
        completer->setCompletionPrefix(editor->text().mid(begin, length));
        if (completer->completionCount() == 0) completer->popup()->hide();
        else completer->complete();
    });
    connect(completer, qOverload<const QString&>(&QCompleter::activated), editor,
        [editor](const QString& name) {
            const auto [begin, length] = wordAt(editor->text(), editor->cursorPosition());
            editor->setText(editor->text().replace(begin, length, name));
            editor->setCursorPosition(begin + static_cast<int>(name.size()));
        });
}

auto SymbolCells::takeExpressionEdit(QTableWidgetItem* item, std::function<void()> restore) -> bool
{
    const auto field = fieldOf_(*item);
    if (!field) return false;
    const auto typed = item->text().trimmed();
    const auto written = item->data(CellExpressionRole).toString();
    if (written.isEmpty() && parsesAs<double>(typed)) return false;
    const auto below = table_->viewport()->mapToGlobal(table_->visualItemRect(item).bottomLeft());
    QTimer::singleShot(0, this, [this, field = *field, typed, written, below,
                                    restore = std::move(restore)] {
        if (typed != written && !typed.isEmpty())
            emit fieldExpressionEntered(field.sourceLine, field.fieldIndex, field.label, typed, below);
        restore();
    });
    return true;
}

void SymbolCells::showExpression(QTableWidgetItem* item, QTableWidget* table,
    const QString& expression, const QString& value)
{
    item->setText(value);
    item->setData(CellExpressionRole, expression);
    styleParameterControlledField(item, table, expression);
    item->setToolTip(QObject::tr("ƒx %1 = %2\nType over it to change it; change a variable "
        "under Variables.").arg(expression, value));
}

auto SymbolCells::usesExpression(const QTableWidgetItem& item) -> bool
{
    return !item.data(CellExpressionRole).toString().isEmpty();
}

void SymbolCells::showMenu(QPoint position)
{
    const auto* item = table_->itemAt(position);
    if (item == nullptr) return;
    const auto field = fieldOf_(*item);
    if (!field) return;
    const auto expression = usesExpression(*item);
    if (!expression && (!(item->flags() & Qt::ItemIsEditable) || item->text().trimmed().isEmpty()))
        return;
    switch (execVariableMenu(table_, table_->viewport()->mapToGlobal(position), expression)) {
    case VariableChoice::Make:
        emit fieldParameterizationRequested(field->sourceLine, field->fieldIndex, field->label);
        break;
    case VariableChoice::Detach:
        emit fieldDetachmentRequested(field->sourceLine, field->fieldIndex, field->label);
        break;
    case VariableChoice::None: break;
    }
}

}
