#include "ui/optimization/CandidateEvaluator.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>

auto main(int argc, char* argv[]) -> int
{
    if (argc == 3 && QByteArray(argv[1]).startsWith("-i")
        && QByteArray(argv[2]).startsWith("-o")) {
        QFile output(QString::fromLocal8Bit(argv[2] + 2));
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return EXIT_FAILURE;
        const QByteArray solverOutput =
            " FREQUENCY : 7.0000E+00 MHz\n"
            " --------- ANTENNA INPUT PARAMETERS ---------\n"
            " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n"
            " FREQUENCY : 1.4000E+01 MHz\n"
            " --------- ANTENNA INPUT PARAMETERS ---------\n"
            " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 7.5E+01 1.5E+01 0 0 5.0E-03\n";
        return output.write(solverOutput) == solverOutput.size() ? EXIT_SUCCESS : EXIT_FAILURE;
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
    const auto valid = finished
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
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
