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

auto shippedDocuments() -> QStringList
{
    return {QStringLiteral("user-guide.md"), QStringLiteral("nec-card-support.md"),
        QStringLiteral("nec-validation-reference.md")};
}

auto documentFolder(const QString& applicationDirectory) -> QString
{
    const QDir application(applicationDirectory.isEmpty()
        ? QCoreApplication::applicationDirPath() : applicationDirectory);
    for (const auto& candidate : {application.filePath(QStringLiteral("doc")),
             application.filePath(QStringLiteral("../share/nec-workbench/doc")),
             application.filePath(QStringLiteral("../docs")), application.filePath(QStringLiteral("docs")),
             QDir::current().filePath(QStringLiteral("docs"))}) {
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("user-guide.md"))))
            return QDir::cleanPath(QDir(candidate).absolutePath());
    }
    return {};
}

auto onlineUserGuide() -> QUrl
{
    return QUrl(QStringLiteral("https://github.com/fizhoo/NEC-Workbench/blob/v%1/docs/user-guide.md")
        .arg(QStringLiteral(NECWB_DISPLAY_VERSION)));
}

auto isInsideInstallation(const QString& path, const QString& applicationDirectory) -> bool
{
    if (isInsideApplication(path, applicationDirectory)) return true;
    QDir prefix(applicationDirectory.isEmpty() ? QCoreApplication::applicationDirPath() : applicationDirectory);
    if (prefix.dirName() != QStringLiteral("bin") || !prefix.cdUp()) return false;
    return !pathInsideFolder(path, prefix.absolutePath()).isEmpty();
}

}
