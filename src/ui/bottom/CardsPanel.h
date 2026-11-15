#pragma once

#include "nec/NecCardFieldEditor.h"
#include "nec/NecDocument.h"

#include <QWidget>

#include <array>

class QComboBox;
class QLabel;
class QTabWidget;

namespace necwb::ui {

class StructuredCardEditor;
class WireCardEditor;

// The Cards tab of the bottom panel: a table per card type across the window's width, under
// the geometry. Wires use the wire table; sources, loads, transmission lines, networks and the
// remaining cards use the field tables, which keep SY symbols and parameterize fields.
class CardsPanel final : public QWidget {
    Q_OBJECT

public:
    enum class Page { Wires, Sources, Loads, TransmissionLines, Networks, Other };

    explicit CardsPanel(QWidget* parent = nullptr);

    [[nodiscard]] auto wires() const -> WireCardEditor* { return wires_; }
    // The deck's geometry unit, and the scale a GS card gives it.
    [[nodiscard]] auto deckUnits() const -> QComboBox* { return deckUnits_; }
    [[nodiscard]] auto deckScale() const -> QLabel* { return deckScale_; }
    void setDocument(const nec::NecDocument& document, const nec::NecParameterFieldMap& fields);
    void setDeckUnitLabel(const QString& unitLabel);
    void showPage(Page page);
    [[nodiscard]] auto currentPage() const -> Page;
    // Selects the card's row in whichever table has it, without changing the page.
    void selectCard(std::size_t sourceLine);
    void focusParameterizableField();

signals:
    void cardSelected(std::size_t sourceLine);
    void cardEdited(std::size_t sourceLine, QString cardText);
    void cardAddRequested(QString cardText);
    void cardDeleteRequested(std::size_t sourceLine);
    void fieldParameterizationRequested(std::size_t sourceLine,
        std::size_t fieldIndex, QString fieldLabel);
    void fieldDetachmentRequested(std::size_t sourceLine,
        std::size_t fieldIndex, QString fieldLabel);

private:
    QTabWidget* tabs_{};
    WireCardEditor* wires_{};
    // Sources, loads, transmission lines, networks, other.
    std::array<StructuredCardEditor*, 5> cards_{};
    QComboBox* deckUnits_{};
    QLabel* deckScale_{};
};

}
