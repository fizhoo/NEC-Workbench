#include "ui/SettingsCleanup.h"

#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <utility>

namespace necwb::ui {
namespace {

const auto VersionKey = QStringLiteral("ui/layoutVersion");

// Version 1: what the earlier separate windows and main window state saved.
void retireWindowSettings(QSettings& settings)
{
    const auto legacyPath = settings.value(QStringLiteral("analysis/executablePath")).toString();
    const auto backendPath = QStringLiteral("analysis/executables/%1").arg(
        settings.value(QStringLiteral("analysis/backendId"), QStringLiteral("nec2")).toString());
    if (!legacyPath.isEmpty() && settings.value(backendPath).toString().isEmpty())
        settings.setValue(backendPath, legacyPath);
    for (const auto* key : {"workspaceWindows", "resultWindows", "mainWindow/geometry",
             "mainWindow/normalGeometry", "mainWindow/state", "modelCheckWindow/geometry",
             "runMonitorWindow/geometry", "analysis/executablePath"})
        settings.remove(QString::fromLatin1(key));
}

// Version 2: Workbench used to save the nec2c and OpenNEC it found on the PATH as if the user
// had picked them. A saved path that is what the PATH gives today is dropped, so the bundled
// nec2c is found first; any other saved path, and the engine, stay.
void retireFoundSolverPaths(QSettings& settings)
{
    for (const auto& [backendId, name] : {std::pair{"nec2", "nec2c"}, std::pair{"opennec", "onec"}}) {
        const auto key = QStringLiteral("analysis/executables/%1").arg(QString::fromLatin1(backendId));
        const auto saved = QFileInfo(settings.value(key).toString()).canonicalFilePath();
        const auto found = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!saved.isEmpty() && !found.isEmpty() && saved == QFileInfo(found).canonicalFilePath())
            settings.remove(key);
    }
}

}

void retireOldSettings()
{
    QSettings settings;
    const auto version = settings.value(VersionKey, 0).toInt();
    if (version < 1) {
        retireWindowSettings(settings);
        settings.setValue(VersionKey, 1);
    }
    if (version < 2) {
        retireFoundSolverPaths(settings);
        settings.setValue(VersionKey, 2);
    }
}

}
