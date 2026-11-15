#include "ui/cards/CardTables.h"

#include "model/LengthUnit.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/cards/WireCardEditor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {
namespace {

constexpr auto index(CardTables::Table table) -> std::size_t
{
    return static_cast<std::size_t>(table);
}


// A page with a line of controls above its table.
auto withBar(QWidget* bar, QWidget* table, QWidget* host) -> QWidget*
{
    auto* page = new QWidget(host);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(bar);
    layout->addWidget(table, 1);
    return page;
}

}

CardTables::CardTables(QWidget* sourcesBar, QWidget* host) : QObject(host)
{
    wires_ = new WireCardEditor(host);
    auto* units = new QWidget(host);
    auto* unitsLayout = new QHBoxLayout(units);
    unitsLayout->setContentsMargins(4, 0, 4, 0);
    unitsLayout->addWidget(new QLabel(tr("Deck units"), units));
    deckUnits_ = new QComboBox(units);
    deckUnits_->setObjectName(QStringLiteral("deckGeometryUnitControl"));
    for (const auto& [name, unit] : std::initializer_list<std::pair<QString, int>>{
             {tr("Meters"), static_cast<int>(model::LengthUnit::Meter)},
             {tr("Feet"), static_cast<int>(model::LengthUnit::Foot)},
             {tr("Inches"), static_cast<int>(model::LengthUnit::Inch)},
             {tr("Centimeters"), static_cast<int>(model::LengthUnit::Centimeter)},
             {tr("Millimeters"), static_cast<int>(model::LengthUnit::Millimeter)},
             {tr("Custom / mixed GS scale"), -1}}) {
        deckUnits_->addItem(name, unit);
    }
    deckScale_ = new QLabel(units);
    unitsLayout->addWidget(deckUnits_);
    unitsLayout->addWidget(deckScale_, 1);
    pages_[index(Table::Wires)] = withBar(units, wires_, host);
    pages_[index(Table::Wires)]->setObjectName(QStringLiteral("cardsWires"));

    const std::array<QString, 4> single{QStringLiteral("EX"), QStringLiteral("LD"),
        QStringLiteral("TL"), QStringLiteral("NT")};
    for (std::size_t card = 0; card < single.size(); ++card) {
        cards_[card] = new StructuredCardEditor({{single[card]}}, host);
        cards_[card]->setObjectName(QStringLiteral("cards%1").arg(single[card]));
        pages_[card + 1] = cards_[card];
    }
    cards_.back() = new StructuredCardEditor({{single.begin(), single.end()}, true}, host);
    cards_.back()->setObjectName(QStringLiteral("cardsOther"));
    pages_[index(Table::Other)] = cards_.back();
    pages_[index(Table::Sources)] = withBar(sourcesBar, cards_.front(), host);

    for (auto* cards : cards_) {
        connect(cards, &StructuredCardEditor::cardSelected, this, &CardTables::cardSelected);
        connect(cards, &StructuredCardEditor::cardActivated, this, &CardTables::cardActivated);
        connect(cards, &StructuredCardEditor::cardEdited, this, &CardTables::cardEdited);
        connect(cards, &StructuredCardEditor::cardAddRequested, this, &CardTables::cardAddRequested);
        connect(cards, &StructuredCardEditor::cardDeleteRequested,
            this, &CardTables::cardDeleteRequested);
        connect(cards, &StructuredCardEditor::fieldParameterizationRequested,
            this, &CardTables::fieldParameterizationRequested);
        connect(cards, &StructuredCardEditor::fieldDetachmentRequested,
            this, &CardTables::fieldDetachmentRequested);
    }
}

auto CardTables::tableOf(const QWidget* page) const -> std::optional<Table>
{
    const auto found = std::ranges::find(pages_, page);
    if (page == nullptr || found == pages_.end()) return std::nullopt;
    return static_cast<Table>(found - pages_.begin());
}

void CardTables::setDocument(const nec::NecDocument& document,
    const nec::NecParameterFieldMap& fields)
{
    for (auto* cards : cards_) {
        cards->setParameterControlledFields(fields);
        cards->setDocument(document);
    }
}

void CardTables::setDeckUnitLabel(const QString& unitLabel)
{
    for (auto* cards : cards_) cards->setDeckUnitLabel(unitLabel);
}

void CardTables::selectCard(std::size_t sourceLine)
{
    for (auto* cards : cards_) {
        const QSignalBlocker blocker(cards);
        if (cards->selectCard(sourceLine)) return;
    }
}

void CardTables::focusParameterizableField(Table table)
{
    if (table == Table::Wires) wires_->focusParameterizableField();
    else cards_[index(table) - 1]->focusParameterizableField();
}

auto CardTables::fullWidth(Table table) const -> int
{
    const auto* page = pages_[index(table)];
    auto* grid = page->findChild<QTableWidget*>();
    if (grid == nullptr) return page->sizeHint().width();
    // Every column at the width its contents ask for; the columns size to their contents,
    // except the last, which stretches, so it gets its header's width and room for a number.
    constexpr auto NumberWidth = 64;
    auto columns = 0;
    const auto last = grid->columnCount() - 1;
    for (auto column = 0; column <= last; ++column) {
        if (grid->isColumnHidden(column)) continue;
        columns += column < last ? grid->columnWidth(column)
            : std::max(grid->horizontalHeader()->sectionSizeHint(column), NumberWidth);
    }
    const auto rows = grid->verticalHeader()->isVisible() ? grid->verticalHeader()->width() : 0;
    const auto frame = 2 * grid->frameWidth() + grid->verticalScrollBar()->sizeHint().width();
    // The page's own margins and the pane's, around the table.
    const auto around = page->width() - grid->width() + 24;
    return std::max(page->minimumSizeHint().width(), columns + rows + frame + std::max(0, around));
}

}
