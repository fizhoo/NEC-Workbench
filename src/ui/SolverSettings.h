#pragma once

#include <QHash>
#include <QString>

namespace necwb::ui::solverSettings {

// The only writes of the engine settings: the engine the user picked and the executable
// paths the user typed or browsed to. A path that was found (bundled or on the PATH) is never
// saved, and neither is one inside Workbench's own folder or a mounted AppImage, which move
// between launches.

// "nec2" (nec2c) until the user picks another engine.
[[nodiscard]] auto chosenEngine() -> QString;
void chooseEngine(const QString& backendId);
[[nodiscard]] auto chosenPaths() -> QHash<QString, QString>;
// Saves the path the user picked, or forgets it when empty. Refuses, saving nothing, a path
// inside Workbench's own folder.
[[nodiscard]] auto choosePath(const QString& backendId, const QString& path) -> bool;

}
