#pragma once

#include "model/AntennaModel.h"

#include <QElapsedTimer>
#include <QObject>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

class QTimer;
class QWidget;

namespace necwb::ui {

// Redraws the wires with one variable changed, while it is being dragged or typed: the SY
// values and wire coordinates only, with no model check, no deck change and no solver. At most
// one redraw per screen frame, always with the latest value.
class GeometryPreview final : public QObject {
public:
    GeometryPreview(QWidget* host, std::function<std::string_view()> source,
        std::function<void(const model::AntennaModel&)> show);

    void request(const std::string& variable, double value);
    // Drops a redraw not yet made, as when the value is committed.
    void cancel();

private:
    void redraw();

    QWidget* host_{};
    QTimer* frame_{};
    std::function<std::string_view()> source_;
    std::function<void(const model::AntennaModel&)> show_;
    std::optional<std::pair<std::string, double>> pending_;
    QElapsedTimer lastRedraw_;
};

}
