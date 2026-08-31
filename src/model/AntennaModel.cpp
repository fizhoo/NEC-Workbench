#include "model/AntennaModel.h"

#include <algorithm>
#include <utility>

namespace necwb::model {

void AntennaModel::addWire(Wire wire)
{
    wires_.push_back(std::move(wire));
}

void AntennaModel::scale(double factor) noexcept
{
    for (auto& wire : wires_) {
        wire.start.x *= factor;
        wire.start.y *= factor;
        wire.start.z *= factor;
        wire.end.x *= factor;
        wire.end.y *= factor;
        wire.end.z *= factor;
        wire.radius *= factor;
    }
}

auto AntennaModel::wireByTag(int tag) noexcept -> Wire*
{
    const auto found = std::ranges::find(wires_, tag, &Wire::tag);
    return found == wires_.end() ? nullptr : &*found;
}

auto AntennaModel::wireByTag(int tag) const noexcept -> const Wire*
{
    const auto found = std::ranges::find(wires_, tag, &Wire::tag);
    return found == wires_.end() ? nullptr : &*found;
}

auto AntennaModel::wires() const noexcept -> std::span<const Wire>
{
    return wires_;
}

auto AntennaModel::empty() const noexcept -> bool
{
    return wires_.empty();
}

auto AntennaModel::wireCount() const noexcept -> std::size_t
{
    return wires_.size();
}

}
