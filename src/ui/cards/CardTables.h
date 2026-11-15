#pragma once

#include "nec/NecCardFieldEditor.h"
#include "nec/NecDocument.h"

#include <QObject>

#include <array>
#include <optional>

class QComboBox;
class QLabel;
class QWidget;

namespace necwb::ui {

class StructuredCardEditor;
class WireCardEditor;

// The card tables shown in the properties pane when a group is selected: wires in the wire
// table; sources, loads, transmission lines, networks and the remaining cards in the field
// tables, which keep SY symbols and parameterize fields. A row's click selects its item;
// a double-click on its Line cell, or Enter, opens its form.
class CardTables final : public QObject {
    Q_OBJECT

public:
    enum class Table { Wires, Sources, Loads, TransmissionLines, Networks, Other };

    explicit CardTables(QWidget* host);

    [[nodiscard]] auto page(Table table) const -> QWidget*
    {
        return pages_[static_cast<std::size_t>(table)];
    }
    [[nodiscard]] auto tableOf(const QWidget* page) const -> std::optional<Table>;
    [[nodiscard]] auto wires() const -> WireCardEditor* { return wires_; }
    // The deck's geometry unit, and the scale a GS card gives it, above the wire table.
    [[nodiscard]] auto deckUnits() const -> QComboBox* { return deckUnits_; }
    [[nodiscard]] auto deckScale() const -> QLabel* { return deckScale_; }
    void setDocument(const nec::NecDocument& document, const nec::NecParameterFieldMap& fields);
    void setDeckUnitLabel(const QString& unitLabel);
    // Selects the card's row in whichever table has it.
    void selectCard(std::size_t sourceLine);
    void focusParameterizableField(Table table);
    // The card a table's Add would add; empty for the wire table, which adds its own way.
    [[nodiscard]] auto newCard(Table table) const -> QString;

signals:
    void cardSelected(std::size_t sourceLine);
    void cardActivated(std::size_t sourceLine);
    void cardEdited(std::size_t sourceLine, QString cardText);
    void cardAddRequested(QString cardText);
    void cardDeleteRequested(std::size_t sourceLine);
    void fieldParameterizationRequested(std::size_t sourceLine,
        std::size_t fieldIndex, QString fieldLabel);
    void fieldDetachmentRequested(std::size_t sourceLine,
        std::size_t fieldIndex, QString fieldLabel);

private:

    std::array<QWidget*, 6> pages_{};
    WireCardEditor* wires_{};
    // Sources, loads, transmission lines, networks, other.
    std::array<StructuredCardEditor*, 5> cards_{};
    QComboBox* deckUnits_{};
    QLabel* deckScale_{};
};

}
