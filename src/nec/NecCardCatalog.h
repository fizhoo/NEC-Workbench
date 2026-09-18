#pragma once

#include "nec/NecCard.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace necwb::nec {

enum class NecCardArea {
    Comments,
    Parameters,
    Geometry,
    Environment,
    FrequencySources,
    LoadsNetworks,
    RequestsExecution,
    Extensions
};

enum class NecCardSupport {
    Preserved,
    Understood,
    StructuredEditable
};

struct NecCardSpec {
    std::string_view mnemonic;
    std::string_view name;
    NecCardKind kind;
    NecCardArea area;
    NecCardSupport support;
    std::size_t integerFieldCount;
    std::size_t numericFieldCount;
};

auto necCardCatalog() noexcept -> std::span<const NecCardSpec>;
auto findNecCardSpec(std::string_view mnemonic) noexcept -> const NecCardSpec*;
auto isGeometryCard(NecCardKind kind) noexcept -> bool;
auto isControlCard(NecCardKind kind) noexcept -> bool;

}
