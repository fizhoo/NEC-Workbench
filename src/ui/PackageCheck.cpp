#include "ui/PackageCheck.h"

#include "analysis/NecOutputParser.h"
#include "analysis/SolverCommand.h"
#include "ui/SolverLocator.h"
#include "ui/SolverSettings.h"
#include "ui/analysis/SolverProcessRunner.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <cstdio>

namespace necwb::ui {
namespace {

// A dipole over real ground: a quick solve with an impedance to read.
constexpr auto QuickDeck = "CM package check\nCE\nGW 1 21 -5 0 9 5 0 9 0.001\nGE 0\n"
                           "GN 2 0 0 0 13 0.005\nEX 0 1 11 0 1 0\nFR 0 1 0 0 14.2 0\n"
                           "RP 0 19 4 1000 0 0 5 90\nEN\n";
// Long enough that stopping it, or a one-second timeout, ends it rather than nec2c.
constexpr auto LongDeck = "CM package check: long run\nCE\nGW 1 1201 -20 0 9 20 0 9 0.001\nGE 0\n"
                          "GN 2 0 0 0 13 0.005\nEX 0 1 601 0 1 0\nFR 0 200 0 0 3 0.05\nXQ\nEN\n";

struct Run {
    SolverProcessResult result;
    double stoppedAfterSeconds{-1.0};
    QByteArray output;
};

// Runs nec2c on the deck in a folder whose path has a space, stopping it after stopAfter
// milliseconds when that is given.
auto solve(const QString& executable, const char* deck, int timeoutSeconds, int stopAfter = -1) -> Run
{
    Run run;
    QTemporaryDir root;
    const auto folder = QDir(root.path()).filePath(QStringLiteral("package check"));
    QDir().mkpath(folder);
    QFile input(QDir(folder).filePath(QStringLiteral("model.nec")));
    if (!input.open(QIODevice::WriteOnly)) return run;
    input.write(deck);
    input.close();
    SolverProcessRunner runner;
    QEventLoop loop;
    QElapsedTimer stopped;
    QObject::connect(&runner, &SolverProcessRunner::finished, &loop,
        [&](const SolverProcessResult& result) {
            run.result = result;
            if (stopped.isValid()) run.stoppedAfterSeconds = stopped.elapsed() / 1000.0;
            loop.quit();
        });
    if (stopAfter >= 0) {
        QTimer::singleShot(stopAfter, &runner, [&] {
            stopped.start();
            runner.cancel();
        });
    }
    if (timeoutSeconds > 0 && stopAfter < 0) {
        QObject::connect(&runner, &SolverProcessRunner::started, &loop, [&] { stopped.start(); });
    }
    runner.start(analysis::buildSolverCommand("nec2", executable.toStdString(), "model.nec", "model.out"),
        folder, timeoutSeconds);
    loop.exec();
    QFile output(QDir(folder).filePath(QStringLiteral("model.out")));
    if (output.open(QIODevice::ReadOnly)) run.output = output.readAll();
    return run;
}

}

auto runPackageCheck() -> int
{
    // Never the user's settings: a fresh, throwaway store, as on a first start.
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());

    QTextStream out(stdout);
    auto failures = 0;
    const auto report = [&](bool passed, const QString& step, const QString& detail) {
        out << (passed ? "PASS " : "FAIL ") << step << ": " << detail << '\n';
        out.flush();
        failures += passed ? 0 : 1;
    };

    const auto located = locateSolver(QStringLiteral("nec2"),
        solverSettings::chosenPaths().value(QStringLiteral("nec2")));
    report(located.origin == LocatedSolver::Origin::Bundled && !located.missing(),
        QStringLiteral("bundled nec2c found"), solverDescription(QStringLiteral("nec2"), located));
    const auto version = SolverVersions::instance().version(QStringLiteral("nec2"), located.path);
    report(version.startsWith(QStringLiteral("nec2c ")) && version.contains(QLatin1Char('(')),
        QStringLiteral("version from build metadata"), version.isEmpty() ? QStringLiteral("none") : version);
    report(QFileInfo(located.path).isExecutable(), QStringLiteral("nec2c is executable"), located.path);
    if (located.missing()) return 1;

    const auto quick = solve(located.path, QuickDeck, 60);
    const auto result = analysis::NecOutputParser{}.parse(quick.output.toStdString());
    const auto solved = quick.result.status == SolverProcessStatus::Succeeded
        && !result.feedpoints.empty() && std::isfinite(result.feedpoints.front().impedance.real());
    report(solved, QStringLiteral("solve"), solved
        ? QStringLiteral("Z = %1 %2 j%3 Ω at 14.2 MHz, %4 pattern points")
              .arg(result.feedpoints.front().impedance.real(), 0, 'f', 3)
              .arg(result.feedpoints.front().impedance.imag() < 0 ? QLatin1Char('-') : QLatin1Char('+'))
              .arg(std::abs(result.feedpoints.front().impedance.imag()), 0, 'f', 3)
              .arg(result.radiation.size())
        : quick.result.detail);

    // Stop and the timeout end the process themselves: terminate, then kill.
    const auto stop = solve(located.path, LongDeck, 600, 1500);
    report(stop.result.status == SolverProcessStatus::Canceled && stop.stoppedAfterSeconds >= 0.0
            && stop.stoppedAfterSeconds < 10.0,
        QStringLiteral("stop"), QStringLiteral("ended %1 s after Stop").arg(stop.stoppedAfterSeconds, 0, 'f', 2));
    const auto timeout = solve(located.path, LongDeck, 1);
    report(timeout.result.status == SolverProcessStatus::TimedOut && timeout.stoppedAfterSeconds < 10.0,
        QStringLiteral("timeout"), QStringLiteral("ended %1 s after starting, with a 1 s timeout")
            .arg(timeout.stoppedAfterSeconds, 0, 'f', 2));

    out << (failures == 0 ? "PACKAGE CHECK PASSED\n" : "PACKAGE CHECK FAILED\n");
    return failures == 0 ? 0 : 1;
}

}
