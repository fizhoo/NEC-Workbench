#pragma once

#include <QPoint>
#include <QString>

#include <cstddef>

class QWidget;

namespace necwb::ui {

enum class VariableChoice { None, Make, Detach };

// The right-click menu on a numeric card field: Make Variable… (Change Variable… for a field
// that already uses one, which also offers Replace With Current Numeric Value).
[[nodiscard]] auto execVariableMenu(QWidget* parent, QPoint globalPosition, bool usesVariable)
    -> VariableChoice;

// A GW card field's name, by field index: Tag, Segments, X1 … Z2, Radius.
[[nodiscard]] auto wireFieldName(std::size_t field) -> QString;

}
