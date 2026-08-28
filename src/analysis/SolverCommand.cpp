#include "analysis/SolverCommand.h"

#include <stdexcept>
#include <utility>

namespace necwb::analysis {

auto isBackendRunnable(const std::string& backendId) -> bool
{
    return backendId == "nec2";
}

auto buildSolverCommand(const std::string& backendId, std::string executable,
    const std::string& inputPath, const std::string& outputPath) -> SolverCommand
{
    if (!isBackendRunnable(backendId)) {
        throw std::invalid_argument("No process adapter is available for the selected backend");
    }
    return {
        std::move(executable),
        {"-i" + inputPath, "-o" + outputPath},
    };
}

}
