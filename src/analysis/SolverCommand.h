#pragma once

#include <string>
#include <vector>

namespace necwb::analysis {

struct SolverCommand {
    std::string executable;
    std::vector<std::string> arguments;
};

[[nodiscard]] auto isBackendRunnable(const std::string& backendId) -> bool;
[[nodiscard]] auto buildSolverCommand(const std::string& backendId, std::string executable,
    const std::string& inputPath, const std::string& outputPath) -> SolverCommand;

}
