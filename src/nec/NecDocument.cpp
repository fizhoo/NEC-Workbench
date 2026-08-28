#include "nec/NecDocument.h"

#include <utility>

namespace necwb::nec {

NecDocument::NecDocument(std::string lineEnding)
    : lineEnding_(std::move(lineEnding))
{
}

void NecDocument::addCard(NecCard card)
{
    cards_.push_back(std::move(card));
}

void NecDocument::setHasFinalLineEnding(bool value) noexcept
{
    hasFinalLineEnding_ = value;
}

auto NecDocument::cards() const & noexcept -> std::span<const NecCard>
{
    return cards_;
}

auto NecDocument::lineEnding() const & noexcept -> const std::string&
{
    return lineEnding_;
}

auto NecDocument::hasFinalLineEnding() const noexcept -> bool
{
    return hasFinalLineEnding_;
}

}
