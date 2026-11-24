#pragma once

#include <QTemporaryDir>

class QWidget;

namespace necwb::ui {

// A fresh settings store in a temporary folder for the lifetime of this object, as on a
// first start: the checks never read or write the user's settings.
class ThrowawaySettings {
public:
    ThrowawaySettings();

private:
    QTemporaryDir folder_;
};

// `nec-workbench --package-check`, for CI on the built packages: with throwaway settings and
// no window, finds the bundled nec2c beside the executable, reads its version, runs a solve,
// and stops a long solve both with Stop and with a timeout. Prints a PASS or FAIL line per
// step and returns 0 only when all pass.
[[nodiscard]] auto runPackageCheck() -> int;

// `nec-workbench --startup-check`, for CI on the built packages: the normal start with
// throwaway settings, which prints a PASS line with the platform plugin and the Qt version in
// use and quits with 0 once the main window is shown on screen, or prints FAIL and quits
// with 1 after 30 s.
// A missing platform plugin stops the start before this, with a non-zero exit.
void watchStartup(QWidget& window);

}
