#include "ui/SolverSettings.h"

#include "ui/SolverLocator.h"

#include <QSettings>

namespace necwb::ui::solverSettings {
namespace {

const auto EngineKey = QStringLiteral("analysis/backendId");

auto pathKey(const QString& backendId) -> QString
{
    return QStringLiteral("analysis/executables/%1").arg(backendId);
}

}

auto chosenEngine() -> QString
{
    return QSettings{}.value(EngineKey, QStringLiteral("nec2")).toString();
}

void chooseEngine(const QString& backendId)
{
    QSettings{}.setValue(EngineKey, backendId);
}

auto chosenPaths() -> QHash<QString, QString>
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("analysis/executables"));
    QHash<QString, QString> paths;
    for (const auto& backendId : settings.childKeys()) {
        const auto path = settings.value(backendId).toString();
        if (!path.isEmpty()) paths.insert(backendId, path);
    }
    return paths;
}

auto choosePath(const QString& backendId, const QString& path) -> bool
{
    if (backendId.isEmpty() || (!path.isEmpty() && isInsideApplication(path))) return false;
    QSettings settings;
    if (path.isEmpty()) settings.remove(pathKey(backendId));
    else settings.setValue(pathKey(backendId), path);
    return true;
}

}
