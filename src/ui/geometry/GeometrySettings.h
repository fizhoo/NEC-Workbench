#pragma once

#include "model/LengthUnit.h"

namespace necwb::ui {

enum class SnapUnitBehavior {
    UnitFriendly,
    PreservePhysical
};

struct GeometrySettings {
    model::LengthUnit lengthUnit{model::LengthUnit::Meter};
    bool automaticGridSpacing{true};
    double manualGridSpacingMeters{1.0};
    int minorGridDivisions{5};
    bool showGrid{true};
    bool showAxes{true};
    bool showLabels{true};
    bool gridSnapping{true};
    double snapSpacingMeters{0.5};
    SnapUnitBehavior snapUnitBehavior{SnapUnitBehavior::UnitFriendly};
    bool endpointSnapping{true};
    double endpointTolerancePixels{12.0};
};

}
