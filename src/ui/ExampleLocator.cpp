#include "ui/ExampleLocator.h"

#include "ui/SolverLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace necwb::ui {

auto exampleFolder(const QString& applicationDirectory) -> QString
{
    const QDir application(applicationDirectory.isEmpty()
        ? QCoreApplication::applicationDirPath() : applicationDirectory);
    for (const auto& candidate : {application.filePath(QStringLiteral("examples")),
             application.filePath(QStringLiteral("../share/nec-workbench/examples")),
             QDir::current().filePath(QStringLiteral("examples"))}) {
        if (QDir(candidate).exists()) return QDir::cleanPath(QDir(candidate).absolutePath());
    }
    return {};
}

auto isInsideInstallation(const QString& path, const QString& applicationDirectory) -> bool
{
    if (isInsideApplication(path, applicationDirectory)) return true;
    QDir prefix(applicationDirectory.isEmpty() ? QCoreApplication::applicationDirPath() : applicationDirectory);
    if (prefix.dirName() != QStringLiteral("bin") || !prefix.cdUp()) return false;
    return !pathInsideFolder(path, prefix.absolutePath()).isEmpty();
}

}
