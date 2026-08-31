#pragma once

#include "model/Wire.h"

#include <span>
#include <vector>

namespace necwb::model {

class AntennaModel {
public:
    void addWire(Wire wire);
    void scale(double factor) noexcept;
    [[nodiscard]] auto wireByTag(int tag) noexcept -> Wire*;
    [[nodiscard]] auto wireByTag(int tag) const noexcept -> const Wire*;
    [[nodiscard]] auto wires() const noexcept -> std::span<const Wire>;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto wireCount() const noexcept -> std::size_t;

private:
    std::vector<Wire> wires_;
};

}
