#include "ui/optimization/CandidateEvaluator.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace {

auto writeSolverOutput(const QString& path) -> bool
{
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray solverOutput =
        " FREQUENCY : 7.0000E+00 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n"
        " FREQUENCY : 1.4000E+01 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 7.5E+01 1.5E+01 0 0 5.0E-03\n";
    return output.write(solverOutput) == solverOutput.size();
}

}

auto main(int argc, char* argv[]) -> int
{
    if (argc == 3 && QByteArray(argv[1]).startsWith("-i")
        && QByteArray(argv[2]).startsWith("-o")) {
        return writeSolverOutput(QString::fromLocal8Bit(argv[2] + 2))
            ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 6 && QByteArray(argv[1]) == "-f"
        && QByteArray(argv[2]) == "original" && QByteArray(argv[3]) == "-o") {
        return writeSolverOutput(QString::fromLocal8Bit(argv[4]))
            ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 1 && QFileInfo::exists(QStringLiteral("nec2dxs-test.marker"))) {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly)) return EXIT_FAILURE;
        const auto filenames = QString::fromLocal8Bit(input.readAll())
                                   .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        return filenames.size() >= 2 && filenames[0] == QStringLiteral("model.nec")
                && writeSolverOutput(filenames[1])
            ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return EXIT_FAILURE;

    necwb::ui::CandidateEvaluator evaluator;
    necwb::ui::CandidateEvaluationResult evaluation;
    auto finished = false;
    QEventLoop loop;
    QObject::connect(&evaluator, &necwb::ui::CandidateEvaluator::finished,
        [&evaluation, &finished, &loop](auto result) {
            evaluation = std::move(result);
            finished = true;
            loop.quit();
        });
    evaluator.start({
        .authoredSource = QStringLiteral(
            "SY HALF=5\nSY HEIGHT=6\nGW 1 21 -HALF 0 HEIGHT HALF 0 HEIGHT 0.001\nGE 0\n"
            "EX 0 1 11 0 1 0\nFR 0 2 0 0 7 0.1\nRP 0 19 37 1000 0 0 5 10\nEN\n"),
        .variableValues = {{"half", 6.0}, {"height", 8.0}},
        .frequencyPlan = {necwb::analysis::FrequencyPlanMode::ModelSweep, {7.0, 14.0}, {}},
        .objective = {.kind = necwb::analysis::OptimizationObjectiveKind::SwrAtFrequency,
            .referenceImpedance = 50.0, .targetFrequencyMHz = 14.0},
        .backend = QStringLiteral("nec2"),
        .executable = QCoreApplication::applicationFilePath(),
        .directory = directory.path(),
        .timeoutSeconds = 2,
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QFile candidateDeck(QDir(directory.path()).filePath(QStringLiteral("model.nec")));
    const auto deck = candidateDeck.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(candidateDeck.readAll()) : QString{};
    QFile candidateSource(QDir(directory.path()).filePath(QStringLiteral("model.source.nec")));
    const auto source = candidateSource.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(candidateSource.readAll()) : QString{};
    auto valid = finished
        && evaluation.status == necwb::ui::CandidateEvaluationStatus::Completed
        && evaluation.frequencyCount == 2 && evaluation.analysis.feedpoints.size() == 2
        && evaluation.objective && evaluation.objective->feedpoint
        && evaluation.objective->feedpoint->frequencyMHz == 14.0
        && deck.contains(QStringLiteral("GW 1 21 -6 0 8 6 0 8 0.001"))
        && deck.contains(QStringLiteral("FR 0 1 0 0 14 0"))
        && !deck.contains(QStringLiteral("FR 0 2 0 0 7 0.1"))
        && !deck.contains(QStringLiteral("FR 0 1 0 0 7 0"))
        && !deck.contains(QStringLiteral("SY ")) && !deck.contains(QStringLiteral("RP "))
        && source.startsWith(QStringLiteral("SY HALF=5"));

    const auto evaluateBackend = [&application](const QString& backend) {
        QTemporaryDir backendDirectory;
        if (!backendDirectory.isValid()) return false;
        if (backend == QStringLiteral("nec2dxs")) {
            QFile marker(QDir(backendDirectory.path()).filePath(
                QStringLiteral("nec2dxs-test.marker")));
            if (!marker.open(QIODevice::WriteOnly)) return false;
        }
        necwb::ui::CandidateEvaluator backendEvaluator;
        necwb::ui::CandidateEvaluationResult result;
        auto backendFinished = false;
        QEventLoop backendLoop;
        QObject::connect(&backendEvaluator, &necwb::ui::CandidateEvaluator::finished,
            [&result, &backendFinished, &backendLoop](auto evaluationResult) {
                result = std::move(evaluationResult);
                backendFinished = true;
                backendLoop.quit();
            });
        backendEvaluator.start({
            .authoredSource = QStringLiteral(
                "SY HALF=5\nGW 1 21 -HALF 0 8 HALF 0 8 0.001\nGE 0\n"
                "EX 0 1 11 0 1 0\nFR 0 1 0 0 14 0\nXQ 0\nEN\n"),
            .variableValues = {{"half", 6.0}},
            .frequencyPlan = {necwb::analysis::FrequencyPlanMode::Explicit, {14.0}, {}},
            .objective = {.kind = necwb::analysis::OptimizationObjectiveKind::SwrAtFrequency,
                .referenceImpedance = 50.0, .targetFrequencyMHz = 14.0},
            .backend = backend,
            .executable = QCoreApplication::applicationFilePath(),
            .directory = backendDirectory.path(),
            .timeoutSeconds = 2,
        });
        QTimer::singleShot(2000, &backendLoop, &QEventLoop::quit);
        backendLoop.exec();
        application.processEvents();
        return backendFinished
            && result.status == necwb::ui::CandidateEvaluationStatus::Completed
            && result.analysis.feedpoints.size() == 2;
    };
    valid = valid && evaluateBackend(QStringLiteral("opennec"))
        && evaluateBackend(QStringLiteral("nec2dxs"));
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
