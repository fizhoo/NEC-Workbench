#include "ui/geometry/EngineeringSpinBox.h"

#include "model/LengthUnit.h"

namespace necwb::ui {

EngineeringSpinBox::EngineeringSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
{
}

void EngineeringSpinBox::stepBy(int steps)
{
    auto steppedValue = value();
    while (steps > 0) {
        steppedValue = model::nextEngineeringStep(steppedValue);
        --steps;
    }
    while (steps < 0) {
        steppedValue = model::previousEngineeringStep(steppedValue);
        ++steps;
    }
    setValue(steppedValue);
}

}
