#include "analysis/FrequencyPlan.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace necwb::analysis {
namespace {

constexpr std::size_t MaximumExpandedPoints = 100000;

void appendPoint(std::vector<double>& points, double frequencyMHz)
{
    if (std::isfinite(frequencyMHz) && frequencyMHz > 0.0
        && points.size() < MaximumExpandedPoints)
        points.push_back(frequencyMHz);
}

void appendRange(std::vector<double>& points, const FrequencyRange& range)
{
    if (!std::isfinite(range.startMHz) || !std::isfinite(range.endMHz)
        || !std::isfinite(range.stepMHz) || range.startMHz <= 0.0
        || range.endMHz < range.startMHz || range.stepMHz <= 0.0)
        return;

    const auto span = range.endMHz - range.startMHz;
    const auto intervalCount = static_cast<std::size_t>(
        std::floor(span / range.stepMHz + 1.0e-9));
    for (std::size_t index = 0;
         index <= intervalCount && points.size() < MaximumExpandedPoints; ++index)
        appendPoint(points, range.startMHz + static_cast<double>(index) * range.stepMHz);

    if (points.size() < MaximumExpandedPoints) appendPoint(points, range.endMHz);
}

}

auto frequencyPlanPoints(const FrequencyPlan& plan) -> std::vector<double>
{
    auto points = plan.pointsMHz;
    points.reserve(std::min(MaximumExpandedPoints,
        points.size() + plan.ranges.size() * 16U));
    for (const auto& range : plan.ranges) appendRange(points, range);
    std::erase_if(points, [](double frequencyMHz) {
        return !std::isfinite(frequencyMHz) || frequencyMHz <= 0.0;
    });
    std::ranges::sort(points);
    const auto duplicates = std::ranges::unique(points, [](double left, double right) {
        const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
        return std::abs(left - right) <= std::numeric_limits<double>::epsilon() * scale * 16.0;
    });
    points.erase(duplicates.begin(), duplicates.end());
    if (points.size() > MaximumExpandedPoints) points.resize(MaximumExpandedPoints);
    return points;
}

auto amateurBandPresets() -> const std::vector<AmateurBandPreset>&
{
    static const std::vector<AmateurBandPreset> presets{
        {"160 m", 1.8, 2.0, 0.025},
        {"80 m", 3.5, 4.0, 0.05},
        {"40 m", 7.0, 7.3, 0.05},
        {"30 m", 10.1, 10.15, 0.01},
        {"20 m", 14.0, 14.35, 0.05},
        {"17 m", 18.068, 18.168, 0.025},
        {"15 m", 21.0, 21.45, 0.05},
        {"12 m", 24.89, 24.99, 0.025},
        {"10 m", 28.0, 29.7, 0.1},
        {"6 m", 50.0, 54.0, 0.2},
        {"2 m", 144.0, 148.0, 0.5},
    };
    return presets;
}

}
