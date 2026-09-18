#include "model/ModelSetup.h"

#include "model/AntennaModel.h"
#include "model/WireGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace necwb::model {

auto referenceImpedanceOhms(const ModelSetup& setup) -> double
{
    return setup.referenceImpedance ? setup.referenceImpedance->ohms : 50.0;
}

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
    return wireSegmentPosition(model, excitation.wireTag, excitation.segment);
}

auto wireSegmentPosition(const AntennaModel& model, int wireTag, int segment)
    -> std::optional<Point3D>
{
    const auto* wire = model.wireByTag(wireTag);
    return wire == nullptr ? std::nullopt : wireSegmentCenter(*wire, segment);
}

auto loadPosition(const AntennaModel& model, const LoadDefinition& load)
    -> std::optional<Point3D>
{
    const auto* wire = model.wireByTag(load.wireTag);
    if (wire == nullptr) return std::nullopt;
    const auto first = load.firstSegment == 0 ? 1 : load.firstSegment;
    const auto last = load.lastSegment == 0 ? wire->segments : load.lastSegment;
    if (first < 1 || last < first || last > wire->segments) return std::nullopt;
    const auto total = first + last;
    if (total % 2 == 0) return wireSegmentCenter(*wire, total / 2);
    const auto lower = wireSegmentEndpoints(*wire, total / 2);
    return lower ? std::optional{lower->second} : std::nullopt;
}

}
