#include "ui/bottom/CardsPanel.h"

#include "model/LengthUnit.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/cards/WireCardEditor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace necwb::ui {

CardsPanel::CardsPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("cardsPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("cardsTabs"));
    tabs_->setDocumentMode(true);
    layout->addWidget(tabs_);
    wires_ = new WireCardEditor(tabs_);
    tabs_->addTab(wires_, tr("Wires"));
    const std::array<std::pair<QString, QString>, 4> single{{{QStringLiteral("EX"), tr("Sources")},
        {QStringLiteral("LD"), tr("Loads")}, {QStringLiteral("TL"), tr("Transmission lines")},
        {QStringLiteral("NT"), tr("Networks")}}};
    QStringList named;
    for (std::size_t index = 0; index < single.size(); ++index) {
        cards_[index] = new StructuredCardEditor({{single[index].first}}, tabs_);
        cards_[index]->setObjectName(QStringLiteral("cards%1").arg(single[index].first));
        tabs_->addTab(cards_[index], single[index].second);
        named.append(single[index].first);
    }
    cards_.back() = new StructuredCardEditor({named, true}, tabs_);
    cards_.back()->setObjectName(QStringLiteral("cardsOther"));
    tabs_->addTab(cards_.back(), tr("Other"));

    // The deck's geometry unit sits beside the tabs, where the wire coordinates use it.
    auto* units = new QWidget(tabs_);
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
    unitsLayout->addWidget(deckScale_);
    tabs_->setCornerWidget(units, Qt::TopRightCorner);

    for (auto* cards : cards_) {
        connect(cards, &StructuredCardEditor::cardSelected, this, &CardsPanel::cardSelected);
        connect(cards, &StructuredCardEditor::cardEdited, this, &CardsPanel::cardEdited);
        connect(cards, &StructuredCardEditor::cardAddRequested, this, &CardsPanel::cardAddRequested);
        connect(cards, &StructuredCardEditor::cardDeleteRequested,
            this, &CardsPanel::cardDeleteRequested);
        connect(cards, &StructuredCardEditor::fieldParameterizationRequested,
            this, &CardsPanel::fieldParameterizationRequested);
        connect(cards, &StructuredCardEditor::fieldDetachmentRequested,
            this, &CardsPanel::fieldDetachmentRequested);
    }
}

void CardsPanel::setDocument(const nec::NecDocument& document,
    const nec::NecParameterFieldMap& fields)
{
    for (auto* cards : cards_) {
        cards->setParameterControlledFields(fields);
        cards->setDocument(document);
    }
}

void CardsPanel::setDeckUnitLabel(const QString& unitLabel)
{
    for (auto* cards : cards_) cards->setDeckUnitLabel(unitLabel);
}

void CardsPanel::showPage(Page page)
{
    tabs_->setCurrentIndex(static_cast<int>(page));
}

auto CardsPanel::currentPage() const -> Page
{
    return static_cast<Page>(tabs_->currentIndex());
}

void CardsPanel::selectCard(std::size_t sourceLine)
{
    for (auto* cards : cards_) {
        const QSignalBlocker blocker(cards);
        if (cards->selectCard(sourceLine)) return;
    }
}

void CardsPanel::focusParameterizableField()
{
    if (currentPage() == Page::Wires) wires_->focusParameterizableField();
    else cards_[static_cast<std::size_t>(tabs_->currentIndex() - 1)]->focusParameterizableField();
}

}
