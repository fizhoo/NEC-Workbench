#include "analysis/SolverCommand.h"

#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <utility>

namespace necwb::analysis {

auto isBackendRunnable(const std::string& backendId) -> bool
{
    return backendId == "nec2" || backendId == "opennec"
        || backendId == "nec2dxs";
}

auto buildSolverCommand(const std::string& backendId, std::string executable,
    const std::string& inputPath, const std::string& outputPath) -> SolverCommand
{
    if (!isBackendRunnable(backendId)) {
        throw std::invalid_argument("No process adapter is available for the selected backend");
    }
    if (backendId == "nec2") {
        return {std::move(executable), {"-i" + inputPath, "-o" + outputPath}, {}};
    }
    if (backendId == "opennec") {
        return {std::move(executable),
            {"-f", "original", "-o", outputPath, inputPath}, {}};
    }
    return {std::move(executable), {}, inputPath + "\n" + outputPath + "\n"};
}

auto solverSegmentCapacity(const std::string& backendId,
    const std::string& executable) -> std::optional<int>
{
    if (backendId != "nec2dxs") return std::nullopt;
    auto name = std::filesystem::path(executable).stem().string();
    std::ranges::transform(name, name.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    constexpr std::pair<std::string_view, int> capacities[]{
        {"nec2dxs11k", 11000},
        {"nec2dxs8k0", 8000},
        {"nec2dxs5k0", 5000},
        {"nec2dxs3k0", 3000},
        {"nec2dxs1k5", 1500},
        {"nec2dxs500", 500},
    };
    for (const auto& [marker, capacity] : capacities) {
        if (name.find(marker) != std::string::npos) return capacity;
    }
    return std::nullopt;
}

}
