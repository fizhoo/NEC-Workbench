#pragma once

#include <QPoint>
#include <QString>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

class QWidget;

namespace necwb::ui {

struct FieldExpressionEdit {
    // The new source, or none when the user cancelled or the expression is invalid.
    std::optional<std::string> source;
    // Why the expression could not be used; empty when it was cancelled.
    QString error;
};

// An expression typed into a card field, such as len or len/2: the field takes it, and each
// name it uses that is not yet defined becomes an SY card at the top of the deck. A box under
// the cell, at `below`, asks for their values, starting from the value that keeps the field
// as it is when that is simple to work out.
[[nodiscard]] auto enterFieldExpression(QWidget* parent, QPoint below, std::string_view source,
    std::size_t sourceLine, std::size_t fieldIndex, const QString& expression) -> FieldExpressionEdit;

}
