#pragma once

#include <QPoint>
#include <QString>

#include <cstddef>

class QWidget;

namespace necwb::ui {

enum class VariableChoice { None, Make, Detach, Center, AlongWire, ExactSegment };

// Where a voltage source's segment field puts it: no choice for any other field, a segment,
// or a position along the wire.
enum class SourcePlacing { None, Segment, AlongWire };

// The right-click menu on a numeric card field: Make Variable… (Change Variable… for a field
// that already uses one, which also offers Replace With Current Numeric Value). A source's
// segment also offers Center of Wire and Along Wire…, and Exact Segment once it is placed along
// its wire, which replaces the variable items.
[[nodiscard]] auto execVariableMenu(QWidget* parent, QPoint globalPosition, bool usesVariable,
    SourcePlacing placing = SourcePlacing::None) -> VariableChoice;

// A GW card field's name, by field index: Tag, Segments, X1 … Z2, Radius.
[[nodiscard]] auto wireFieldName(std::size_t field) -> QString;

}
