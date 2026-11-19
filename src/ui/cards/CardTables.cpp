#include "ui/cards/CardTables.h"

#include "model/LengthUnit.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/cards/SymbolCells.h"
#include "ui/cards/WireCardEditor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
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

CardTables::CardTables(QWidget* host) : QObject(host)
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

    for (auto* cards : cards_) {
        connect(cards, &StructuredCardEditor::cardSelected, this, &CardTables::cardSelected);
        connect(cards, &StructuredCardEditor::cardEdited, this, &CardTables::cardEdited);
        connect(cards, &StructuredCardEditor::cardAddRequested, this, &CardTables::cardAddRequested);
        connect(cards, &StructuredCardEditor::cardDeleteRequested,
            this, &CardTables::cardDeleteRequested);
    }
    // Every table, the wire table too, makes variables and takes expressions the same way.
    std::vector<SymbolCells*> symbolCells{wires_->symbolCells()};
    for (auto* cards : cards_) symbolCells.push_back(cards->symbolCells());
    for (auto* cells : symbolCells) {
        connect(cells, &SymbolCells::fieldParameterizationRequested,
            this, &CardTables::fieldParameterizationRequested);
        connect(cells, &SymbolCells::fieldDetachmentRequested,
            this, &CardTables::fieldDetachmentRequested);
        connect(cells, &SymbolCells::fieldExpressionEntered, this, &CardTables::fieldExpressionEntered);
    }
}

auto CardTables::tableOf(const QWidget* page) const -> std::optional<Table>
{
    const auto found = std::ranges::find(pages_, page);
    if (page == nullptr || found == pages_.end()) return std::nullopt;
    return static_cast<Table>(found - pages_.begin());
}

void CardTables::setDocument(const nec::NecDocument& document,
    const nec::NecDocument* resolved, const std::vector<nec::SymbolDefinition>& symbols)
{
    wires_->setSymbols(symbols);
    for (auto* cards : cards_) {
        cards->setSymbols(symbols);
        cards->setDocument(document, resolved);
    }
}

void CardTables::setDeckUnitLabel(const QString& unitLabel)
{
    for (auto* cards : cards_) cards->setDeckUnitLabel(unitLabel);
}

auto CardTables::selectCard(std::size_t sourceLine) -> std::optional<Table>
{
    for (std::size_t card = 0; card < cards_.size(); ++card) {
        const QSignalBlocker blocker(cards_[card]);
        if (cards_[card]->selectCard(sourceLine)) return static_cast<Table>(card + 1);
    }
    return std::nullopt;
}

void CardTables::setSharedSegments(const std::vector<model::SharedSegment>& shared)
{
    wires_->setSharedSegments(shared);
    for (auto* cards : cards_) cards->setSharedSegments(shared);
}

void CardTables::focusParameterizableField(Table table)
{
    if (table == Table::Wires) wires_->focusParameterizableField();
    else cards_[index(table) - 1]->focusParameterizableField();
}

auto CardTables::newCard(Table table) const -> QString
{
    return table == Table::Wires ? QString{} : cards_[index(table) - 1]->newCard();
}

}
