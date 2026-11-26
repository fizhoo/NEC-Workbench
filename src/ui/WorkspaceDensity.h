#pragma once

#include <QString>
#include <QtGlobal>

class QWidget;

namespace necwb::ui {

// View → Workspace Density: the application font size, and the layout spacing scale MainWindow
// applies to itself, its dialogs and the windows that follow it.
enum class WorkspaceDensity {
    Compact,
    Standard,
    Spacious
};

[[nodiscard]] auto workspaceDensityFromId(const QString& id) -> WorkspaceDensity;
[[nodiscard]] auto workspaceDensityId(WorkspaceDensity density) -> QString;
[[nodiscard]] auto workspaceDensityScale(WorkspaceDensity density) -> qreal;
[[nodiscard]] auto workspaceDensityFontSize(WorkspaceDensity density) -> int;
// The density the user chose last, compact when none.
[[nodiscard]] auto savedWorkspaceDensity() -> WorkspaceDensity;
void saveWorkspaceDensity(WorkspaceDensity density);

// Sets the density's font size on the window and everything in it, each widget keeping the
// offset from the base size it had the first time.
void applyWorkspaceDensityFont(QWidget* root, WorkspaceDensity density);

// A top-level window without a dialog's type (the optimizer) that follows the density like
// the main window's dialogs do.
void followWorkspaceDensity(QWidget* window);
// A window MainWindow brings to the density: a dialog or one marked above.
[[nodiscard]] auto followsWorkspaceDensity(const QWidget* window) -> bool;

}
