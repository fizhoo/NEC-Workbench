#pragma once

#include "nec/NecCard.h"

#include <span>
#include <string>
#include <vector>

namespace necwb::nec {

class NecDocument {
public:
    explicit NecDocument(std::string lineEnding = "\n");

    void addCard(NecCard card);
    void setHasFinalLineEnding(bool value) noexcept;

    [[nodiscard]] auto cards() const noexcept -> std::span<const NecCard>;
    [[nodiscard]] auto lineEnding() const noexcept -> const std::string&;
    [[nodiscard]] auto hasFinalLineEnding() const noexcept -> bool;

private:
    std::vector<NecCard> cards_;
    std::string lineEnding_;
    bool hasFinalLineEnding_{false};
};

}
