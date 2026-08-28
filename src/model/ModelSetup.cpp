#include "model/ModelSetup.h"

#include "model/AntennaModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace necwb::model {

auto frequencyEndMHz(const FrequencyDefinition& frequency) -> double
{
    if (frequency.count <= 1) {
        return frequency.startMHz;
    }
    if (frequency.steppingMode == 1) {
        return frequency.startMHz * std::pow(frequency.step, frequency.count - 1);
    }
    return frequency.startMHz + (frequency.count - 1) * frequency.step;
}

auto frequencyPointCount(int steppingMode, double startMHz, double endMHz, double step)
    -> std::optional<int>
{
    if (startMHz <= 0.0 || endMHz < startMHz || (steppingMode != 0 && steppingMode != 1)) {
        return std::nullopt;
    }
    if (endMHz == startMHz) {
        return 1;
    }
    if ((steppingMode == 0 && step <= 0.0) || (steppingMode == 1 && step <= 1.0)) {
        return std::nullopt;
    }
    const auto intervals = steppingMode == 1
        ? std::log(endMHz / startMHz) / std::log(step)
        : (endMHz - startMHz) / step;
    const auto count = std::floor(intervals + 1.0e-10) + 1.0;
    if (!std::isfinite(count) || count > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return std::max(1, static_cast<int>(count));
}

auto excitationPosition(const AntennaModel& model, const Excitation& excitation)
    -> std::optional<Point3D>
{
    const auto* wire = model.wireByTag(excitation.wireTag);
    if (wire == nullptr || excitation.segment < 1 || excitation.segment > wire->segments) {
        return std::nullopt;
    }
    const auto parameter = (static_cast<double>(excitation.segment) - 0.5) / wire->segments;
    return Point3D{
        wire->start.x + (wire->end.x - wire->start.x) * parameter,
        wire->start.y + (wire->end.y - wire->start.y) * parameter,
        wire->start.z + (wire->end.z - wire->start.z) * parameter};
}

}
