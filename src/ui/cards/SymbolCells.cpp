#include "ui/cards/SymbolCells.h"

#include "ui/ParameterFieldStyle.h"
#include "ui/cards/CardFields.h"
#include "ui/cards/VariableMenu.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QLineEdit>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>

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

// Under the editor, as wide as the longest "name = value" offered or the cell if wider; the
// completer keeps it on the screen.
auto popupRect(const QCompleter& completer, const QLineEdit& editor) -> QRect
{
    const auto* popup = completer.popup();
    const auto width = popup->sizeHintForColumn(0) + 2 * popup->frameWidth()
        + popup->verticalScrollBar()->sizeHint().width();
    return {0, 0, std::max(editor.width(), width), editor.height()};
}

// The cell and the table's viewport on screen, where a box about the cell goes.
auto placeOf(const QTableWidget& table, const QTableWidgetItem& item) -> PromptPlace
{
    const auto* viewport = table.viewport();
    const auto cell = table.visualItemRect(&item);
    return {QRect(viewport->mapToGlobal(cell.topLeft()), cell.size()),
        QRect(viewport->mapToGlobal(QPoint(0, 0)), viewport->size())};
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
        else completer->complete(popupRect(*completer, *editor));
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
    const auto place = placeOf(*table_, *item);
    QTimer::singleShot(0, this, [this, field = *field, typed, written, place,
                                    restore = std::move(restore)] {
        if (typed != written && !typed.isEmpty())
            emit fieldExpressionEntered(field.sourceLine, field.fieldIndex, field.label, typed, place);
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
    auto* item = table_->itemAt(position);
    if (item == nullptr) return;
    const auto field = fieldOf_(*item);
    if (!field) return;
    const auto expression = usesExpression(*item);
    if (!expression && (!(item->flags() & Qt::ItemIsEditable) || item->text().trimmed().isEmpty()))
        return;
    const auto enter = [this, field = *field, item](const QString& text) {
        emit fieldExpressionEntered(field.sourceLine, field.fieldIndex, field.label, text,
            placeOf(*table_, *item));
    };
    switch (execVariableMenu(table_, table_->viewport()->mapToGlobal(position), expression,
        field->placing)) {
    case VariableChoice::Make:
        emit fieldParameterizationRequested(field->sourceLine, field->fieldIndex, field->label);
        break;
    case VariableChoice::Detach:
        emit fieldDetachmentRequested(field->sourceLine, field->fieldIndex, field->label);
        break;
    case VariableChoice::Center:
        enter(QStringLiteral("50%"));
        break;
    case VariableChoice::ExactSegment:
        // The segment it is on now.
        enter(item->text().trimmed());
        break;
    case VariableChoice::AlongWire: {
        // The cell opens on its percentage, or 50%, with the number selected to type over.
        const auto percent = field->placing == SourcePlacing::AlongWire
            ? item->data(CellExpressionRole).toString() : QStringLiteral("50%");
        table_->setCurrentItem(item);
        table_->editItem(item);
        if (auto* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
            editor->setText(percent);
            editor->setSelection(0, static_cast<int>(percent.size()) - 1);
        }
        break;
    }
    case VariableChoice::None: break;
    }
}

}
