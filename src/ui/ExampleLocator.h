#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

namespace necwb::ui {

// The folder "Open Example…" starts in: examples/ beside Workbench (the Windows ZIP), else
// ../share/nec-workbench/examples (an install and the AppImage), else ./examples (a run from
// the source tree). Empty when there is none. The application folder is Workbench's own by
// default.
[[nodiscard]] auto exampleFolder(const QString& applicationDirectory = {}) -> QString;
// The documentation shipped with Workbench, side by side: the user guide and the two pages it
// links to (CMakeLists.txt installs exactly these from docs/).
[[nodiscard]] auto shippedDocuments() -> QStringList;
// The folder whose user-guide.md Help › User Guide opens: doc/ beside Workbench (the Windows
// ZIP), else ../share/nec-workbench/doc (an install and the AppImage), else a source tree's
// docs/ (../docs beside the build folder, docs/ beside Workbench, ./docs). Empty when none.
[[nodiscard]] auto documentFolder(const QString& applicationDirectory = {}) -> QString;
// This version's user guide on GitHub, opened when none is installed.
[[nodiscard]] auto onlineUserGuide() -> QUrl;
// Inside what was shipped: Workbench's folder or the mounted AppImage, or the install prefix
// when Workbench is in its bin/ (bin/ beside share/nec-workbench/examples). Wider than
// isInsideApplication, which must not take in a whole /usr prefix.
[[nodiscard]] auto isInsideInstallation(const QString& path, const QString& applicationDirectory = {})
    -> bool;

}
