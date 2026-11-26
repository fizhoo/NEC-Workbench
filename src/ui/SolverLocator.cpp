#include "ui/SolverLocator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace necwb::ui {
namespace {

constexpr auto ProbeTimeoutMilliseconds = 3000;
const auto BuildMetadata = QStringLiteral("nec2c-build.txt");

auto applicationFolder(const QString& directory) -> QString
{
    return directory.isEmpty() ? QCoreApplication::applicationDirPath() : directory;
}

// The executable names looked for beside Workbench and on the PATH.
auto executableNames(const QString& backendId) -> QStringList
{
    if (backendId == QStringLiteral("nec2")) return {QStringLiteral("nec2c")};
    if (backendId == QStringLiteral("opennec")) return {QStringLiteral("onec")};
    return {};
}

auto find(const QString& backendId, const QStringList& folders) -> QString
{
    for (const auto& name : executableNames(backendId)) {
        const auto path = QStandardPaths::findExecutable(name, folders);
        if (!path.isEmpty()) return QDir::toNativeSeparators(path);
    }
    return {};
}

// "nec2c 1.3.3 (55be1e0)" from the metadata build.sh writes beside the executable.
auto builtVersion(const QString& path) -> QString
{
    QFile file(QFileInfo(path).dir().filePath(BuildMetadata));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QHash<QString, QString> values;
    for (const auto& line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        const auto equals = line.indexOf(QLatin1Char('='));
        if (equals > 0) values.insert(line.left(equals), line.mid(equals + 1).trimmed());
    }
    if (values.value(QStringLiteral("version")).isEmpty()) return {};
    return QStringLiteral("%1 %2 (%3)").arg(values.value(QStringLiteral("name")),
        values.value(QStringLiteral("version")), values.value(QStringLiteral("commit")).left(7));
}

// On Windows only a file with the executable signature, since Windows answers starting
// anything else with a dialog that blocks until someone closes it.
auto looksRunnable(const QString& path) -> bool
{
#ifdef Q_OS_WIN
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.read(2) == "MZ";
#else
    Q_UNUSED(path);
    return true;
#endif
}

auto versionArguments(const QString& backendId) -> QStringList
{
    return backendId == QStringLiteral("nec2") ? QStringList{QStringLiteral("-v")} : QStringList{};
}

}

auto solverName(const QString& backendId) -> QString
{
    if (backendId == QStringLiteral("nec2")) return QStringLiteral("nec2c");
    if (backendId == QStringLiteral("opennec")) return QStringLiteral("OpenNEC");
    if (backendId == QStringLiteral("nec2dxs")) return QStringLiteral("4nec2 NEC2dXS");
    return backendId;
}

auto LocatedSolver::missing() const -> bool
{
    return origin == Origin::NotFound || !QFileInfo(path).isFile();
}

auto locateSolver(const QString& backendId, const QString& chosenPath,
    const QString& applicationDirectory) -> LocatedSolver
{
    if (!chosenPath.isEmpty()) return {chosenPath, LocatedSolver::Origin::Chosen};
    if (const auto bundled = bundledSolver(backendId, applicationDirectory); !bundled.isEmpty())
        return {bundled, LocatedSolver::Origin::Bundled};
    if (const auto onPath = find(backendId, {}); !onPath.isEmpty())
        return {onPath, LocatedSolver::Origin::Path};
    return {};
}

auto locateSolver(const QString& backendId, const QHash<QString, QString>& chosenPaths) -> LocatedSolver
{
    return locateSolver(backendId, chosenPaths.value(backendId));
}

auto bundledSolver(const QString& backendId, const QString& applicationDirectory) -> QString
{
    const QDir folder(applicationFolder(applicationDirectory));
    return find(backendId, {folder.filePath(QStringLiteral("solvers")), folder.path()});
}

auto pathInsideFolder(const QString& path, const QString& folder) -> QString
{
    if (path.isEmpty() || folder.isEmpty()) return {};
    const QFileInfo file(path), directory(folder);
    const auto resolved = file.exists() && directory.exists();
    const auto filePath = resolved ? file.canonicalFilePath() : QDir::cleanPath(file.absoluteFilePath());
    auto folderPath = resolved ? directory.canonicalFilePath() : QDir::cleanPath(directory.absoluteFilePath());
    if (!folderPath.endsWith(QLatin1Char('/'))) folderPath += QLatin1Char('/');
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    if (filePath.size() <= folderPath.size() || !filePath.startsWith(folderPath, sensitivity)) return {};
    return filePath.mid(folderPath.size());
}

auto isInsideApplication(const QString& path, const QString& applicationDirectory) -> bool
{
    return !pathInsideFolder(path, applicationFolder(applicationDirectory)).isEmpty()
        || !pathInsideFolder(path, qEnvironmentVariable("APPDIR")).isEmpty();
}

auto solverLabel(const QString& backendId, const LocatedSolver& located) -> QString
{
    if (located.origin == LocatedSolver::Origin::Bundled)
        return QCoreApplication::translate("SolverLocator", "%1 (bundled)").arg(solverName(backendId));
    if (backendId == QStringLiteral("nec2")) return QStringLiteral("NEC-2 / nec2c");
    return solverName(backendId);
}

auto solverDescription(const QString& backendId, const LocatedSolver& located) -> QString
{
    const auto tr = [](const char* text) { return QCoreApplication::translate("SolverLocator", text); };
    if (located.origin == LocatedSolver::Origin::NotFound)
        return tr("%1 · not found").arg(solverName(backendId));
    if (located.missing())
        return tr("%1: the chosen executable %2 no longer exists. Choose Configure… to pick it "
                  "again, or clear the path to use the one found automatically.")
            .arg(solverName(backendId), QDir::toNativeSeparators(located.path));
    auto version = SolverVersions::instance().version(backendId, located.path);
    if (version.isEmpty()) version = tr("checking version");
    const auto origin = located.origin == LocatedSolver::Origin::Bundled ? tr("bundled")
        : located.origin == LocatedSolver::Origin::Path ? tr("found on PATH") : tr("chosen");
    return QStringLiteral("%1 · %2 · %3").arg(version, origin, QDir::toNativeSeparators(located.path));
}

auto bundledSolverSummary() -> QString
{
    const auto path = bundledSolver(QStringLiteral("nec2"));
    const auto version = path.isEmpty() ? QString{} : builtVersion(path);
    return version.isEmpty()
        ? QCoreApplication::translate("SolverLocator", "No bundled nec2c with this build.")
        : QCoreApplication::translate("SolverLocator",
              "Bundled engine: %1, a separate program under its own license "
              "(see THIRD_PARTY_LICENSES.txt).").arg(version);
}

auto SolverVersions::instance() -> SolverVersions&
{
    static SolverVersions versions;
    return versions;
}

auto SolverVersions::version(const QString& backendId, const QString& path) -> QString
{
    const QFileInfo file(path);
    if (!file.isFile()) return {};
    const auto key = QStringLiteral("%1|%2|%3").arg(file.absoluteFilePath()).arg(file.size())
        .arg(file.lastModified().toMSecsSinceEpoch());
    if (const auto known = versions_.find(key); known != versions_.end()) return *known;
    if (const auto built = builtVersion(path); !built.isEmpty()) return versions_[key] = built;
    // Only an executable named as the engine is asked, so choosing some other program never
    // runs it with an option it may not know.
    const auto arguments = versionArguments(backendId);
    if (arguments.isEmpty() || !file.baseName().startsWith(executableNames(backendId).value(0),
            Qt::CaseInsensitive) || !looksRunnable(path))
        return versions_[key] = QCoreApplication::translate("SolverLocator", "version unknown");
    // Asked once in the background; an executable that does not answer in time is unknown.
    versions_.insert(key, {});
    auto* process = new QProcess(this);
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    const auto finish = [this, process, key](QString answer) {
        if (!versions_.value(key).isEmpty()) return;
        versions_[key] = answer.isEmpty()
            ? QCoreApplication::translate("SolverLocator", "version unknown") : answer;
        process->disconnect();
        process->kill();
        process->deleteLater();
        emit versionFound();
    };
    connect(timer, &QTimer::timeout, this, [finish] { finish({}); });
    connect(process, &QProcess::finished, this, [process, finish] {
        finish(QString::fromLocal8Bit(process->readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0)
                   .trimmed());
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish({});
    });
    process->setStandardInputFile(QProcess::nullDevice());
    process->start(path, arguments);
    timer->start(ProbeTimeoutMilliseconds);
    return {};
}

}
