#include "ui/SettingsCleanup.h"

#include <QSettings>

namespace necwb::ui {

void retireOldSettings()
{
    constexpr auto LayoutVersion = 1;
    const auto versionKey = QStringLiteral("ui/layoutVersion");
    QSettings settings;
    if (settings.value(versionKey, 0).toInt() >= LayoutVersion) return;
    const auto legacyPath = settings.value(QStringLiteral("analysis/executablePath")).toString();
    const auto backendPath = QStringLiteral("analysis/executables/%1").arg(
        settings.value(QStringLiteral("analysis/backendId"), QStringLiteral("nec2")).toString());
    if (!legacyPath.isEmpty() && settings.value(backendPath).toString().isEmpty())
        settings.setValue(backendPath, legacyPath);
    for (const auto* key : {"workspaceWindows", "resultWindows", "mainWindow/geometry",
             "mainWindow/normalGeometry", "mainWindow/state", "modelCheckWindow/geometry",
             "runMonitorWindow/geometry", "analysis/executablePath"})
        settings.remove(QString::fromLatin1(key));
    settings.setValue(versionKey, LayoutVersion);
}

}
