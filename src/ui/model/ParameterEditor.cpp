#include "ui/model/ParameterEditor.h"

#include "ui/DisplayFormat.h"
#include "ui/PendingEditIndicator.h"

#include <QAbstractItemView>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {

constexpr auto SourceLineRole = Qt::UserRole;
constexpr auto OriginalNameRole = Qt::UserRole + 1;
constexpr auto OriginalExpressionRole = Qt::UserRole + 2;
constexpr auto OriginalResolvedValueRole = Qt::UserRole + 3;

constexpr auto NameColumn = 0;
constexpr auto ExpressionColumn = 1;
constexpr auto ResolvedValueColumn = 2;

auto parameterNameIsValid(const QString& name) -> bool
{
    static const QRegularExpression validName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return validName.match(name.trimmed()).hasMatch();
}

}

ParameterEditor::ParameterEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto* group = new QGroupBox(tr("Model Parameters (SY)"), this);
    auto* groupLayout = new QVBoxLayout(group);

    auto* description = new QLabel(tr(
        "Double-click Name or Expression to edit. Resolved values are calculated after Apply Selected. "
        "To choose which NEC field a symbol controls, right-click that numeric field under "
        "NEC Deck → Structured Cards and parameterize or link it there."), group);
    description->setWordWrap(true);
    groupLayout->addWidget(description);

    table_ = new QTableWidget(group);
    table_->setObjectName(QStringLiteral("parameterTable"));
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Expression"), tr("Resolved Value")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked
        | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    table_->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ExpressionColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ResolvedValueColumn, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    groupLayout->addWidget(table_, 1);

    auto* buttons = new QHBoxLayout;
    addButton_ = new QPushButton(tr("Add Parameter"), group);
    addButton_->setObjectName(QStringLiteral("addParameterButton"));
    applyButton_ = new QPushButton(tr("Apply Selected"), group);
    applyButton_->setObjectName(QStringLiteral("applyParameterButton"));
    revertButton_ = new QPushButton(tr("Revert Changes"), group);
    revertButton_->setObjectName(QStringLiteral("revertParameterButton"));
    deleteButton_ = new QPushButton(tr("Delete Selected"), group);
    deleteButton_->setObjectName(QStringLiteral("deleteParameterButton"));
    buttons->addWidget(addButton_);
    buttons->addWidget(applyButton_);
    buttons->addWidget(revertButton_);
    buttons->addWidget(deleteButton_);
    buttons->addStretch();
    groupLayout->addLayout(buttons);

    statusLabel_ = new QLabel(group);
    statusLabel_->setObjectName(QStringLiteral("parameterStatus"));
    statusLabel_->setWordWrap(true);
    groupLayout->addWidget(statusLabel_);
    layout->addWidget(group, 1);

    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        updateActions();
        if (!updating_ && table_->currentRow() >= 0)
            emit parameterSelected(table_->item(table_->currentRow(), NameColumn)
                ->data(SourceLineRole).toULongLong());
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (updating_ || item == nullptr
            || (item->column() != NameColumn && item->column() != ExpressionColumn)) return;
        updatePendingState(item->row());
    });
    connect(addButton_, &QPushButton::clicked, this, [this] { addDraft(); });
    connect(applyButton_, &QPushButton::clicked, this, [this] { applySelected(); });
    connect(revertButton_, &QPushButton::clicked, this, [this] { revertPendingEdits(); });
    connect(deleteButton_, &QPushButton::clicked, this, [this] {
        const auto row = table_->currentRow();
        if (row < 0 || pendingRow_ >= 0) return;
        const auto* item = table_->item(row, NameColumn);
        emit parameterDeleteRequested(item->data(SourceLineRole).toULongLong(),
            item->data(OriginalNameRole).toString());
    });
    updateActions();
}

void ParameterEditor::setResolution(const nec::SymbolResolution& resolution)
{
    updating_ = true;
    const auto selectedLine = table_->currentRow() < 0 ? std::size_t{}
        : table_->item(table_->currentRow(), NameColumn)->data(SourceLineRole).toULongLong();
    const auto selectedName = table_->currentRow() < 0 ? QString{}
        : table_->item(table_->currentRow(), NameColumn)->data(OriginalNameRole).toString();
    definitions_ = resolution.definitions;
    const QSignalBlocker blocker(table_);
    table_->setRowCount(static_cast<int>(definitions_.size()));
    for (std::size_t index = 0; index < definitions_.size(); ++index) {
        const auto& definition = definitions_[index];
        const auto row = static_cast<int>(index);
        const auto name = QString::fromStdString(definition.name);
        const auto expression = QString::fromStdString(definition.expression);
        const auto resolvedValue = formatDecimal(definition.value);
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setData(SourceLineRole, static_cast<qulonglong>(definition.lineNumber));
        nameItem->setData(OriginalNameRole, name);
        nameItem->setData(OriginalExpressionRole, expression);
        nameItem->setData(OriginalResolvedValueRole, resolvedValue);
        nameItem->setToolTip(tr("Defined on source line %1").arg(definition.lineNumber));
        table_->setItem(row, NameColumn, nameItem);
        table_->setItem(row, ExpressionColumn, new QTableWidgetItem(expression));
        auto* valueItem = new QTableWidgetItem(resolvedValue);
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueItem->setToolTip(tr("Resolved numeric value; no physical unit is inferred."));
        table_->setItem(row, ResolvedValueColumn, valueItem);
        if (definition.lineNumber == selectedLine
            && name.compare(selectedName, Qt::CaseInsensitive) == 0)
            table_->selectRow(row);
    }
    pendingRow_ = -1;
    updating_ = false;
    setPendingEditIndicator(applyButton_, false);
    if (resolution.diagnostics.empty()) {
        statusLabel_->setText(definitions_.empty()
            ? tr("No SY parameters are defined. Select Add Parameter to create one.")
            : tr("%1 parameter(s) resolved successfully.").arg(definitions_.size()));
        statusLabel_->setStyleSheet({});
    } else {
        statusLabel_->setText(tr("%1 SY error(s). Use Check Model for line-specific details.")
            .arg(resolution.diagnostics.size()));
        statusLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
    }
    updateRowEditability();
    updateActions();
}

void ParameterEditor::selectParameter(std::size_t sourceLine, const QString& name)
{
    updating_ = true;
    table_->clearSelection();
    for (auto row = 0; row < table_->rowCount(); ++row) {
        const auto* item = table_->item(row, NameColumn);
        if (item->data(SourceLineRole).toULongLong() != sourceLine) continue;
        if (!name.isEmpty()
            && item->data(OriginalNameRole).toString().compare(name, Qt::CaseInsensitive) != 0) continue;
        table_->selectRow(row);
        table_->scrollToItem(item);
        break;
    }
    updating_ = false;
    updateActions();
}

void ParameterEditor::showEditError(const QString& message)
{
    statusLabel_->setText(message);
    statusLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
}

auto ParameterEditor::hasPendingEdits() const noexcept -> bool
{
    return pendingRow_ >= 0;
}

void ParameterEditor::discardPendingEdits()
{
    revertPendingEdits();
}

void ParameterEditor::addDraft()
{
    if (pendingRow_ >= 0) {
        table_->selectRow(pendingRow_);
        table_->editItem(table_->item(pendingRow_, NameColumn));
        return;
    }
    updating_ = true;
    const auto row = table_->rowCount();
    table_->insertRow(row);
    auto* nameItem = new QTableWidgetItem;
    nameItem->setData(SourceLineRole, static_cast<qulonglong>(0));
    nameItem->setData(OriginalNameRole, QString{});
    nameItem->setData(OriginalExpressionRole, QString{});
    nameItem->setData(OriginalResolvedValueRole, QString{});
    table_->setItem(row, NameColumn, nameItem);
    table_->setItem(row, ExpressionColumn, new QTableWidgetItem);
    auto* resolvedItem = new QTableWidgetItem(tr("Not applied"));
    resolvedItem->setFlags(resolvedItem->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, ResolvedValueColumn, resolvedItem);
    pendingRow_ = row;
    table_->selectRow(row);
    updating_ = false;
    updateRowEditability();
    updateActions();
    table_->editItem(nameItem);
}

void ParameterEditor::applySelected()
{
    if (!pendingInputIsValid() || pendingRow_ < 0) return;
    const auto* nameItem = table_->item(pendingRow_, NameColumn);
    emit parameterChanged(nameItem->data(SourceLineRole).toULongLong(),
        nameItem->data(OriginalNameRole).toString(), nameItem->text().trimmed(),
        table_->item(pendingRow_, ExpressionColumn)->text().trimmed());
}

void ParameterEditor::revertPendingEdits()
{
    if (pendingRow_ < 0) return;
    updating_ = true;
    const QSignalBlocker blocker(table_);
    auto* nameItem = table_->item(pendingRow_, NameColumn);
    if (nameItem->data(SourceLineRole).toULongLong() == 0) {
        table_->removeRow(pendingRow_);
    } else {
        nameItem->setText(nameItem->data(OriginalNameRole).toString());
        table_->item(pendingRow_, ExpressionColumn)->setText(
            nameItem->data(OriginalExpressionRole).toString());
        table_->item(pendingRow_, ResolvedValueColumn)->setText(
            nameItem->data(OriginalResolvedValueRole).toString());
        table_->selectRow(pendingRow_);
    }
    pendingRow_ = -1;
    updating_ = false;
    statusLabel_->setStyleSheet({});
    statusLabel_->setText(definitions_.empty()
        ? tr("No SY parameters are defined. Select Add Parameter to create one.")
        : tr("Changes reverted. %1 parameter(s) remain.").arg(definitions_.size()));
    updateRowEditability();
    updateActions();
}

void ParameterEditor::updatePendingState(int row)
{
    if (pendingRow_ >= 0 && row != pendingRow_) return;
    const auto* nameItem = table_->item(row, NameColumn);
    const auto draft = nameItem->data(SourceLineRole).toULongLong() == 0;
    const auto changed = draft
        || nameItem->text().trimmed() != nameItem->data(OriginalNameRole).toString()
        || table_->item(row, ExpressionColumn)->text().trimmed()
            != nameItem->data(OriginalExpressionRole).toString();
    pendingRow_ = changed ? row : -1;
    if (changed) {
        const QSignalBlocker blocker(table_);
        table_->item(row, ResolvedValueColumn)->setText(tr("Apply to resolve"));
        table_->selectRow(row);
    } else {
        const QSignalBlocker blocker(table_);
        table_->item(row, ResolvedValueColumn)->setText(
            nameItem->data(OriginalResolvedValueRole).toString());
        statusLabel_->setStyleSheet({});
        statusLabel_->setText(definitions_.empty()
            ? tr("No SY parameters are defined. Select Add Parameter to create one.")
            : tr("%1 parameter(s) resolved successfully.").arg(definitions_.size()));
    }
    updateRowEditability();
    updateActions();
}

void ParameterEditor::updateRowEditability()
{
    const QSignalBlocker blocker(table_);
    for (auto row = 0; row < table_->rowCount(); ++row) {
        for (const auto column : {NameColumn, ExpressionColumn}) {
            auto* item = table_->item(row, column);
            const auto editable = pendingRow_ < 0 || row == pendingRow_;
            const auto flags = editable ? item->flags() | Qt::ItemIsEditable
                                        : item->flags() & ~Qt::ItemIsEditable;
            if (item->flags() != flags) item->setFlags(flags);
        }
    }
}

void ParameterEditor::updateActions()
{
    const auto selectedRow = table_->currentRow();
    const auto selectedPending = pendingRow_ >= 0 && selectedRow == pendingRow_;
    addButton_->setEnabled(pendingRow_ < 0);
    applyButton_->setEnabled(selectedPending && pendingInputIsValid());
    revertButton_->setEnabled(pendingRow_ >= 0);
    deleteButton_->setEnabled(selectedRow >= 0 && pendingRow_ < 0);
    setPendingEditIndicator(applyButton_, pendingRow_ >= 0);

    if (pendingRow_ < 0) return;
    const auto name = table_->item(pendingRow_, NameColumn)->text().trimmed();
    const auto expression = table_->item(pendingRow_, ExpressionColumn)->text().trimmed();
    statusLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
    if (!parameterNameIsValid(name))
        statusLabel_->setText(tr("Enter a name beginning with a letter or underscore."));
    else if (expression.isEmpty())
        statusLabel_->setText(tr("Enter an expression before applying this parameter."));
    else if (!pendingNameIsUnique())
        statusLabel_->setText(tr("Parameter names must be unique."));
    else {
        statusLabel_->setStyleSheet({});
        statusLabel_->setText(tr("Unapplied parameter changes. Select Apply Selected or Revert Changes."));
    }
}

auto ParameterEditor::pendingInputIsValid() const -> bool
{
    if (pendingRow_ < 0) return false;
    return parameterNameIsValid(table_->item(pendingRow_, NameColumn)->text())
        && !table_->item(pendingRow_, ExpressionColumn)->text().trimmed().isEmpty()
        && pendingNameIsUnique();
}

auto ParameterEditor::pendingNameIsUnique() const -> bool
{
    if (pendingRow_ < 0) return false;
    const auto name = table_->item(pendingRow_, NameColumn)->text().trimmed();
    for (auto row = 0; row < table_->rowCount(); ++row) {
        if (row != pendingRow_
            && table_->item(row, NameColumn)->text().trimmed().compare(
                name, Qt::CaseInsensitive) == 0) return false;
    }
    return true;
}

}
