#pragma once

#include <string>
#include <optional>
#include <vector>

namespace necwb::analysis {

struct SolverCommand {
    std::string executable;
    std::vector<std::string> arguments;
    std::string standardInput;
};

[[nodiscard]] auto isBackendRunnable(const std::string& backendId) -> bool;
[[nodiscard]] auto buildSolverCommand(const std::string& backendId, std::string executable,
    const std::string& inputPath, const std::string& outputPath) -> SolverCommand;
[[nodiscard]] auto solverSegmentCapacity(const std::string& backendId,
    const std::string& executable) -> std::optional<int>;

}
