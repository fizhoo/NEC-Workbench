#include "ui/ExampleLocator.h"

#include <QCoreApplication>
#include <QDir>

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

}
