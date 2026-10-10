#pragma once

#include "nec/NecDocument.h"
#include "nec/NecSymbolResolver.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace necwb::nec {

class NecSourceCache {
public:
    void update(std::string_view source);
    void invalidate() noexcept;

    [[nodiscard]] auto source() const noexcept -> std::string_view;
    [[nodiscard]] auto document() const noexcept -> const NecDocument&;
    [[nodiscard]] auto resolution() const noexcept -> const SymbolResolution&;
    [[nodiscard]] auto resolvedDocument() const noexcept -> const NecDocument*;
    [[nodiscard]] auto revision() const noexcept -> std::size_t;

private:
    std::string source_;
    NecDocument document_;
    SymbolResolution resolution_;
    std::optional<NecDocument> resolvedDocument_;
    std::size_t revision_{};
    bool valid_{};
};

}
