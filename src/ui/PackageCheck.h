#pragma once

namespace necwb::ui {

// `nec-workbench --package-check`, for CI on the built packages: with throwaway settings and
// no window, finds the bundled nec2c beside the executable, reads its version, runs a solve,
// and stops a long solve both with Stop and with a timeout. Prints a PASS or FAIL line per
// step and returns 0 only when all pass.
[[nodiscard]] auto runPackageCheck() -> int;

}
