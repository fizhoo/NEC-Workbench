#pragma once

namespace necwb::ui {

// Once per settings store: removes what earlier layouts saved and nothing reads any more (the
// separate windows' groups and geometry, the old main window state), after moving the single
// solver path of early versions to its backend. ui/layoutVersion records that it ran.
void retireOldSettings();

}
