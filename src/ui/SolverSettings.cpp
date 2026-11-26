#include "ui/SolverSettings.h"

#include "ui/SolverLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace necwb::ui::solverSettings {
namespace {

const auto EngineKey = QStringLiteral("analysis/backendId");

auto pathKey(const QString& backendId) -> QString
{
    return QStringLiteral("analysis/executables/%1").arg(backendId);
}

auto applicationFolder(const QString& directory) -> QDir
{
    return QDir(directory.isEmpty() ? QCoreApplication::applicationDirPath() : directory);
}

// A mounted AppImage: this one ($APPDIR) or any other under /tmp/.mount_*.
auto inAppImage(const QString& file) -> bool
{
    return !pathInsideFolder(file, qEnvironmentVariable("APPDIR")).isEmpty()
        || QDir::cleanPath(file).startsWith(QDir::cleanPath(QDir::tempPath()) + QStringLiteral("/.mount_"))
        || QDir::cleanPath(file).startsWith(QStringLiteral("/tmp/.mount_"));
}

auto isBundled(const QString& file, const QString& applicationDirectory) -> bool
{
    const auto canonical = QFileInfo(file).canonicalFilePath();
    for (const auto& backendId : {QStringLiteral("nec2"), QStringLiteral("opennec")}) {
        const auto bundled = bundledSolver(backendId, applicationDirectory);
        if (!bundled.isEmpty() && !canonical.isEmpty() && QFileInfo(bundled).canonicalFilePath() == canonical)
            return true;
    }
    return false;
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

auto chosenPaths(const QString& applicationDirectory) -> QHash<QString, QString>
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("analysis/executables"));
    const auto application = applicationFolder(applicationDirectory);
    QHash<QString, QString> paths;
    for (const auto& backendId : settings.childKeys()) {
        const auto path = settings.value(backendId).toString();
        if (path.isEmpty()) continue;
        paths.insert(backendId, QDir::isRelativePath(path)
            ? QDir::toNativeSeparators(QDir::cleanPath(application.absoluteFilePath(path))) : path);
    }
    return paths;
}

auto choosePath(const QString& backendId, const QString& path, const QString& applicationDirectory) -> bool
{
    if (backendId.isEmpty()) return false;
    QSettings settings;
    if (path.isEmpty()) {
        settings.remove(pathKey(backendId));
        return true;
    }
    const auto file = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (inAppImage(file) || isBundled(file, applicationDirectory)) return false;
    const auto relative = pathInsideFolder(file, applicationFolder(applicationDirectory).absolutePath());
    settings.setValue(pathKey(backendId), relative.isEmpty() ? path : relative);
    return true;
}

}
