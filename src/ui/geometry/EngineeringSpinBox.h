#pragma once

#include <QDoubleSpinBox>

namespace necwb::ui {

class EngineeringSpinBox final : public QDoubleSpinBox {
public:
    explicit EngineeringSpinBox(QWidget* parent = nullptr);

protected:
    void stepBy(int steps) override;
};

}
