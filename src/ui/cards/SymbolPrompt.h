#pragma once

#include <QRect>
#include <QString>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

class QWidget;

namespace necwb::ui {

// Where a table asks about a cell, on screen: the cell, and the table's viewport, which the
// box stays inside when it can.
struct PromptPlace {
    QRect cell;
    QRect area;
};

struct FieldExpressionEdit {
    // The new source, or none when the user cancelled or the expression is invalid.
    std::optional<std::string> source;
    // Why the expression could not be used; empty when it was cancelled.
    QString error;
};

// An expression typed into a card field, such as len or len/2: the field takes it, and each
// name it uses that is not yet defined becomes an SY card at the top of the deck. A box under
// the cell asks for their values, starting from the value that keeps the field as it is when
// that is simple to work out. In a tag, segment or count it must come to a whole number, as
// the resolver requires.
[[nodiscard]] auto enterFieldExpression(QWidget* parent, const PromptPlace& place, std::string_view source,
    std::size_t sourceLine, std::size_t fieldIndex, const QString& expression) -> FieldExpressionEdit;

}
