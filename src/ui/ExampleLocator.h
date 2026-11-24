#pragma once

#include <QString>

namespace necwb::ui {

// The folder "Open Example…" starts in: examples/ beside Workbench (the Windows ZIP), else
// ../share/nec-workbench/examples (an install and the AppImage), else ./examples (a run from
// the source tree). Empty when there is none. The application folder is Workbench's own by
// default.
[[nodiscard]] auto exampleFolder(const QString& applicationDirectory = {}) -> QString;
// Inside what was shipped: Workbench's folder or the mounted AppImage, or the install prefix
// when Workbench is in its bin/ (bin/ beside share/nec-workbench/examples). Wider than
// isInsideApplication, which keeps chosen engine paths unsaved and must not take in a /usr
// prefix.
[[nodiscard]] auto isInsideInstallation(const QString& path, const QString& applicationDirectory = {})
    -> bool;

}
