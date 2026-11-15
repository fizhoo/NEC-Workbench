#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"
#include "ui/modeltree/ModelTree.h"

#include <QWidget>

#include <array>
#include <functional>

class QToolButton;

namespace necwb::nec { class NecDocument; }

namespace necwb::ui {

// One line above the geometry: the frequency, ground, requests, solver and reference
// impedance, each a part that shows its page in the left pane when clicked. Parts are named
// by the ModelNode of their page.
class SetupLine final : public QWidget {
public:
    explicit SetupLine(QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
        const nec::NecDocument& document);
    void setSolver(const QString& backendId, const QString& executable);
    // Marks the part whose page the pane shows; any other node clears it.
    void setCurrent(ModelNode node);
    void setClickedCallback(std::function<void(ModelNode)> callback);

    // The parts clip rather than widen the center.
    [[nodiscard]] auto minimumSizeHint() const -> QSize override;

private:
    void setPart(ModelNode part, const QString& text, const QString& toolTip);

    std::array<QToolButton*, 5> parts_{};
    std::function<void(ModelNode)> clicked_;
};

}
