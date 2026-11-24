#pragma once

#include <QString>

namespace necwb::ui {

// The folder "Open Example…" starts in: examples/ beside Workbench (the Windows ZIP), else
// ../share/nec-workbench/examples (an install and the AppImage), else ./examples (a run from
// the source tree). Empty when there is none. The application folder is Workbench's own by
// default.
[[nodiscard]] auto exampleFolder(const QString& applicationDirectory = {}) -> QString;

}
