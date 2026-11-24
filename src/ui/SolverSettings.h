#pragma once

#include <QHash>
#include <QString>

namespace necwb::ui::solverSettings {

// The only writes of the engine settings: the engine the user picked and the executable
// paths the user typed or browsed to. A path that was found (bundled or on the PATH) is never
// saved, and neither is the bundled nec2c itself or a path in a mounted AppImage, which moves
// between launches. A path the user picked inside Workbench's own folder (an engine put in a
// portable Windows folder's solvers/) is saved relative to that folder, so it still works
// when the folder moves. The application folder is Workbench's own by default.

// "nec2" (nec2c) until the user picks another engine.
[[nodiscard]] auto chosenEngine() -> QString;
void chooseEngine(const QString& backendId);
// Absolute paths, by engine id; a path saved relative to the application folder is made
// absolute against it.
[[nodiscard]] auto chosenPaths(const QString& applicationDirectory = {}) -> QHash<QString, QString>;
// Saves the path the user picked, or forgets it when empty. Refuses, saving nothing, the
// bundled nec2c and a path in a mounted AppImage.
[[nodiscard]] auto choosePath(const QString& backendId, const QString& path,
    const QString& applicationDirectory = {}) -> bool;

}
