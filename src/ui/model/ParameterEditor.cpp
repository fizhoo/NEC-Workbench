#include "ui/model/ParameterEditor.h"

#include "ui/DisplayFormat.h"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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

}

ParameterEditor::ParameterEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto* group = new QGroupBox(tr("Model Parameters (SY)"), this);
    auto* groupLayout = new QVBoxLayout(group);

    auto* description = new QLabel(tr(
        "SY values are unit-neutral. Their physical meaning comes from expressions and where they are used."), group);
    description->setWordWrap(true);
    groupLayout->addWidget(description);

    table_ = new QTableWidget(group);
    table_->setObjectName(QStringLiteral("parameterTable"));
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Expression"), tr("Resolved Value")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    groupLayout->addWidget(table_, 1);

    auto* form = new QFormLayout;
    nameControl_ = new QLineEdit(group);
    nameControl_->setObjectName(QStringLiteral("parameterName"));
    nameControl_->setPlaceholderText(tr("Example: HALF_LENGTH"));
    expressionControl_ = new QLineEdit(group);
    expressionControl_->setObjectName(QStringLiteral("parameterExpression"));
    expressionControl_->setPlaceholderText(tr("Example: LENGTH_FT*0.3048/2"));
    form->addRow(tr("Name"), nameControl_);
    form->addRow(tr("Expression"), expressionControl_);
    groupLayout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    addButton_ = new QPushButton(tr("Add Parameter"), group);
    addButton_->setObjectName(QStringLiteral("addParameterButton"));
    updateButton_ = new QPushButton(tr("Update Selected"), group);
    updateButton_->setObjectName(QStringLiteral("updateParameterButton"));
    deleteButton_ = new QPushButton(tr("Delete Selected"), group);
    deleteButton_->setObjectName(QStringLiteral("deleteParameterButton"));
    buttons->addWidget(addButton_);
    buttons->addWidget(updateButton_);
    buttons->addWidget(deleteButton_);
    buttons->addStretch();
    groupLayout->addLayout(buttons);

    statusLabel_ = new QLabel(group);
    statusLabel_->setObjectName(QStringLiteral("parameterStatus"));
    statusLabel_->setWordWrap(true);
    groupLayout->addWidget(statusLabel_);
    layout->addWidget(group, 1);

    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        loadSelection();
        updateActions();
        if (!updating_ && table_->currentRow() >= 0)
            emit parameterSelected(table_->item(table_->currentRow(), 0)
                ->data(SourceLineRole).toULongLong());
    });
    connect(nameControl_, &QLineEdit::textChanged, this, [this] { updateActions(); });
    connect(expressionControl_, &QLineEdit::textChanged, this, [this] { updateActions(); });
    connect(addButton_, &QPushButton::clicked, this, [this] {
        emit parameterChanged(0, {}, nameControl_->text().trimmed(),
            expressionControl_->text().trimmed());
    });
    connect(updateButton_, &QPushButton::clicked, this, [this] {
        const auto row = table_->currentRow();
        if (row < 0) return;
        const auto* item = table_->item(row, 0);
        emit parameterChanged(item->data(SourceLineRole).toULongLong(),
            item->data(OriginalNameRole).toString(), nameControl_->text().trimmed(),
            expressionControl_->text().trimmed());
    });
    connect(deleteButton_, &QPushButton::clicked, this, [this] {
        const auto row = table_->currentRow();
        if (row < 0) return;
        const auto* item = table_->item(row, 0);
        emit parameterDeleteRequested(item->data(SourceLineRole).toULongLong(),
            item->data(OriginalNameRole).toString());
    });
    updateActions();
}

void ParameterEditor::setResolution(const nec::SymbolResolution& resolution)
{
    updating_ = true;
    const auto selectedLine = table_->currentRow() < 0 ? std::size_t{}
        : table_->item(table_->currentRow(), 0)->data(SourceLineRole).toULongLong();
    const auto selectedName = table_->currentRow() < 0 ? QString{}
        : table_->item(table_->currentRow(), 0)->data(OriginalNameRole).toString();
    definitions_ = resolution.definitions;
    const QSignalBlocker blocker(table_);
    table_->setRowCount(static_cast<int>(definitions_.size()));
    for (std::size_t index = 0; index < definitions_.size(); ++index) {
        const auto& definition = definitions_[index];
        const auto row = static_cast<int>(index);
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(definition.name));
        nameItem->setData(SourceLineRole, static_cast<qulonglong>(definition.lineNumber));
        nameItem->setData(OriginalNameRole, QString::fromStdString(definition.name));
        nameItem->setToolTip(tr("Defined on source line %1").arg(definition.lineNumber));
        table_->setItem(row, 0, nameItem);
        table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(definition.expression)));
        auto* valueItem = new QTableWidgetItem(formatDecimal(definition.value));
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueItem->setToolTip(tr("Resolved numeric value; no physical unit is inferred."));
        table_->setItem(row, 2, valueItem);
        if (definition.lineNumber == selectedLine
            && QString::fromStdString(definition.name).compare(selectedName, Qt::CaseInsensitive) == 0)
            table_->selectRow(row);
    }
    updating_ = false;
    if (resolution.diagnostics.empty()) {
        statusLabel_->setText(definitions_.empty()
            ? tr("No SY parameters are defined.")
            : tr("%1 parameter(s) resolved successfully.").arg(definitions_.size()));
        statusLabel_->setStyleSheet({});
    } else {
        statusLabel_->setText(tr("%1 SY error(s). Use Check Model for line-specific details.")
            .arg(resolution.diagnostics.size()));
        statusLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
    }
    loadSelection();
    updateActions();
}

void ParameterEditor::selectParameter(std::size_t sourceLine, const QString& name)
{
    updating_ = true;
    table_->clearSelection();
    for (auto row = 0; row < table_->rowCount(); ++row) {
        const auto* item = table_->item(row, 0);
        if (item->data(SourceLineRole).toULongLong() != sourceLine) continue;
        if (!name.isEmpty()
            && item->data(OriginalNameRole).toString().compare(name, Qt::CaseInsensitive) != 0) continue;
        table_->selectRow(row);
        table_->scrollToItem(item);
        break;
    }
    updating_ = false;
    loadSelection();
    updateActions();
}

void ParameterEditor::showEditError(const QString& message)
{
    statusLabel_->setText(message);
    statusLabel_->setStyleSheet(QStringLiteral("color: #9a3030;"));
}

void ParameterEditor::loadSelection()
{
    const auto row = table_->currentRow();
    if (row < 0) return;
    const QSignalBlocker nameBlocker(nameControl_);
    const QSignalBlocker expressionBlocker(expressionControl_);
    nameControl_->setText(table_->item(row, 0)->text());
    expressionControl_->setText(table_->item(row, 1)->text());
}

void ParameterEditor::updateActions()
{
    const auto valid = inputIsValid();
    const auto name = nameControl_->text().trimmed();
    const auto selectedRow = table_->currentRow();
    auto uniqueForAdd = true;
    auto uniqueForUpdate = true;
    for (auto row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, 0)->text().compare(name, Qt::CaseInsensitive) != 0) continue;
        uniqueForAdd = false;
        if (row != selectedRow) uniqueForUpdate = false;
    }
    addButton_->setEnabled(valid && uniqueForAdd);
    updateButton_->setEnabled(valid && uniqueForUpdate && selectedRow >= 0);
    deleteButton_->setEnabled(table_->currentRow() >= 0);
}

auto ParameterEditor::inputIsValid() const -> bool
{
    static const QRegularExpression validName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    const auto name = nameControl_->text().trimmed();
    return validName.match(name).hasMatch() && !expressionControl_->text().trimmed().isEmpty();
}

}
