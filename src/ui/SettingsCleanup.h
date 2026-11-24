#pragma once

namespace necwb::ui {

// Once per settings store, in order of ui/layoutVersion, which records each step that ran:
// 1 removes what earlier layouts saved and nothing reads any more (the separate windows'
// groups and geometry, the old main window state), after moving the single solver path of
// early versions to its backend; 2 drops solver paths that were saved only because they
// were found on the PATH.
void retireOldSettings();

}
