#include "nec/NecSourceCache.h"

#include "nec/NecParser.h"

namespace necwb::nec {

void NecSourceCache::update(std::string_view source)
{
    if (valid_ && source == source_) return;

    source_.assign(source);
    document_ = NecParser{}.parse(source_);
    resolution_ = NecSymbolResolver{}.resolve(source_);
    resolvedDocument_.reset();
    if (resolution_.ok())
        resolvedDocument_.emplace(NecParser{}.parse(resolution_.resolvedSource));
    ++revision_;
    valid_ = true;
}

void NecSourceCache::invalidate() noexcept
{
    source_.clear();
    document_ = NecDocument{};
    resolution_ = SymbolResolution{};
    resolvedDocument_.reset();
    valid_ = false;
}

auto NecSourceCache::source() const noexcept -> std::string_view
{
    return source_;
}

auto NecSourceCache::document() const noexcept -> const NecDocument&
{
    return document_;
}

auto NecSourceCache::resolution() const noexcept -> const SymbolResolution&
{
    return resolution_;
}

auto NecSourceCache::resolvedDocument() const noexcept -> const NecDocument*
{
    return resolvedDocument_ ? &*resolvedDocument_ : nullptr;
}

auto NecSourceCache::revision() const noexcept -> std::size_t
{
    return revision_;
}

}
