#pragma once

#include <QHash>
#include <QObject>
#include <QString>

namespace necwb::ui {

// Where an engine's executable is: the path the user chose, else a copy shipped in solvers/
// beside Workbench (the home of bundled engines) or beside Workbench itself (the AppImage's
// usr/bin), else the system PATH. A chosen path that no longer
// exists stays chosen and is reported, never swapped for another engine's.
struct LocatedSolver {
    enum class Origin { Chosen, Bundled, Path, NotFound };
    QString path;
    Origin origin{Origin::NotFound};

    // A chosen path that is gone, or nothing found.
    [[nodiscard]] auto missing() const -> bool;
};

// The application folder is Workbench's own by default.
[[nodiscard]] auto locateSolver(const QString& backendId, const QString& chosenPath,
    const QString& applicationDirectory = {}) -> LocatedSolver;
// With the chosen paths of every engine, by id.
[[nodiscard]] auto locateSolver(const QString& backendId, const QHash<QString, QString>& chosenPaths)
    -> LocatedSolver;
[[nodiscard]] auto bundledSolver(const QString& backendId, const QString& applicationDirectory = {})
    -> QString;
// The path relative to the folder, when it is inside it (never the folder itself). Paths are
// made absolute and cleaned, symlinks resolved when both exist, and compared ignoring case
// only on Windows. Empty when it is not inside, or either is empty.
[[nodiscard]] auto pathInsideFolder(const QString& path, const QString& folder) -> QString;
// Inside Workbench's own folder or the mounted AppImage, which move between launches.
[[nodiscard]] auto isInsideApplication(const QString& path, const QString& applicationDirectory = {})
    -> bool;

// The name users see for an engine id; ids stay internal.
[[nodiscard]] auto solverName(const QString& backendId) -> QString;
// "nec2c (bundled)" when the bundled copy is the one in use, else the engine's name.
[[nodiscard]] auto solverLabel(const QString& backendId, const LocatedSolver& located) -> QString;
// "nec2c 1.3.3 (55be1e0) · bundled · /path", for the engine list's tooltip and the Run log.
[[nodiscard]] auto solverDescription(const QString& backendId, const LocatedSolver& located) -> QString;
// The bundled nec2c for Help › About.
[[nodiscard]] auto bundledSolverSummary() -> QString;

// Each executable's version: from the nec2c-build.txt written beside a bundled build, or
// from the engine's version option, asked in the background with a short timeout and kept
// by path, size and modification time.
class SolverVersions final : public QObject {
    Q_OBJECT

public:
    static auto instance() -> SolverVersions&;

    // The version, "version unknown" when it cannot be told, or empty while it is asked.
    [[nodiscard]] auto version(const QString& backendId, const QString& path) -> QString;

signals:
    void versionFound();

private:
    SolverVersions() = default;

    QHash<QString, QString> versions_;
};

}
