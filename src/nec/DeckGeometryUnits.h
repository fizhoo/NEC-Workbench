#pragma once

#include "model/LengthUnit.h"
#include "nec/NecDocument.h"

#include <cstddef>
#include <optional>

namespace necwb::nec {

struct DeckGeometryUnitInfo {
    double scaleToMeters{1.0};
    std::optional<model::LengthUnit> standardUnit{model::LengthUnit::Meter};
    std::size_t scaleCardLine{};
    bool hasScaleCard{};
    bool uniform{true};
};

[[nodiscard]] auto geometryScaleFactor(const NecCard& card) noexcept -> std::optional<double>;
[[nodiscard]] auto geometryScaleForLine(const NecDocument& document, std::size_t sourceLine) noexcept
    -> double;
[[nodiscard]] auto inspectDeckGeometryUnits(const NecDocument& document) noexcept
    -> DeckGeometryUnitInfo;
[[nodiscard]] auto standardLengthUnit(double scaleToMeters) noexcept
    -> std::optional<model::LengthUnit>;

}
