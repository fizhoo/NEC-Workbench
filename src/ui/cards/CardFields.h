#pragma once

#include <QString>

#include <vector>

namespace necwb::ui {

// A value a NEC card field can take from a list, such as an LD type, with its label.
struct FieldChoice {
    QString value;
    QString label;
};

// The listed values for a field, or none for a field typed as a number.
[[nodiscard]] auto cardFieldChoices(const QString& mnemonic, int fieldIndex)
    -> std::vector<FieldChoice>;
// The label for a value of a listed field, or the value itself.
[[nodiscard]] auto cardFieldChoiceLabel(const QString& mnemonic, int fieldIndex,
    const QString& value) -> QString;
// What a source, load, transmission-line or network field means, with its unit, for a card of
// the given type (the EX or LD type field): "R Ω" for a series RLC load, "σ S/m" for wire
// conductivity. Empty past the card's fields.
[[nodiscard]] auto cardFieldLabel(const QString& mnemonic, int fieldIndex, int type) -> QString;
// The number of fields cardFieldLabel names for the card and type.
[[nodiscard]] auto cardFieldCount(const QString& mnemonic, int type) -> int;

}
