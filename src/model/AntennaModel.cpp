#include "model/AntennaModel.h"

#include <algorithm>
#include <utility>

namespace necwb::model {

void AntennaModel::addWire(Wire wire)
{
    wires_.push_back(std::move(wire));
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
