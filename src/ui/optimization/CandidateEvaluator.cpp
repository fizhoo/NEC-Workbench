#include "ui/optimization/CandidateEvaluator.h"

#include "analysis/NecOutputParser.h"
#include "analysis/SolverCommand.h"
#include "analysis/SolverInput.h"
#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"
#include "nec/NecSymbolResolver.h"
#include "ui/FileIo.h"

#include <QDir>
#include <QTimer>

#include <algorithm>
#include <exception>
#include <set>
#include <utility>

namespace necwb::ui {
namespace {

auto solverOutputSummary(const QByteArray& output) -> QString
{
    const auto lines = QString::fromLocal8Bit(output).split(QLatin1Char('\n'));
    for (auto line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        constexpr auto maximumLength = 160;
        if (line.size() > maximumLength)
            line = line.left(maximumLength - 1) + QChar(0x2026);
        return line;
    }
    return {};
}

auto objectiveFailureDetail(const analysis::OptimizationObjectiveFailure& failure,
    const analysis::OptimizationObjectiveSpec& objective) -> QString
{
    const auto frequency = failure.frequencyMHz
        ? QObject::tr(" at %1 MHz").arg(*failure.frequencyMHz, 0, 'f', 3) : QString{};
    const auto direction = objective.directionalEvaluationMode
            == analysis::DirectionalEvaluationMode::SpecifiedDirection
        ? QObject::tr(" (theta %1 deg, phi %2 deg)")
              .arg(objective.forwardThetaDegrees, 0, 'f', 3)
              .arg(objective.forwardPhiDegrees, 0, 'f', 3)
        : objective.directionalEvaluationMode
                == analysis::DirectionalEvaluationMode::StrongestInAzimuthCut
            ? QObject::tr(" (strongest lobe in theta %1 deg azimuth cut)")
                  .arg(objective.forwardThetaDegrees, 0, 'f', 3)
            : QObject::tr(" (strongest lobe in phi %1 deg elevation cut)")
                  .arg(objective.forwardPhiDegrees, 0, 'f', 3);
    using Kind = analysis::OptimizationObjectiveFailureKind;
    switch (failure.kind) {
    case Kind::InvalidConfiguration:
        return QObject::tr("The optimization objective configuration is invalid.");
    case Kind::NoFrequencyResults:
        return QObject::tr("No frequency results were parsed from the solver output.");
    case Kind::MissingImpedance:
        return QObject::tr("Impedance results are missing%1.").arg(frequency);
    case Kind::InvalidStandingWaveRatio:
        return QObject::tr("SWR is undefined%1 for the configured reference impedance.")
            .arg(frequency);
    case Kind::MissingForwardDirection:
        return QObject::tr("Forward gain is unavailable%1%2: the requested direction is not present in the radiation samples.")
            .arg(frequency, direction);
    case Kind::NullForwardGain:
        return QObject::tr("Forward gain is unavailable%1%2: NEC returned a pattern null (-999.99 dBi).")
            .arg(frequency, direction);
    case Kind::MissingBackDirection:
        return QObject::tr("F/B is unavailable%1%2: the physical 180-degree opposite direction is not present in the radiation samples.")
            .arg(frequency, direction);
    case Kind::NullBackGain:
        return QObject::tr("F/B is unavailable%1%2: NEC returned a pattern null (-999.99 dBi) in the opposite direction.")
            .arg(frequency, direction);
    case Kind::MissingRearCoverage:
        return QObject::tr("F/R is unavailable%1%2: the required rear cut is not present in the radiation samples.")
            .arg(frequency, direction);
    case Kind::NullRearGain:
        return QObject::tr("F/R is unavailable%1%2: the rear cut contains only NEC pattern-null samples (-999.99 dBi).")
            .arg(frequency, direction);
    case Kind::Unknown:
        return QObject::tr("Required objective results are unavailable.");
    }
    return QObject::tr("Required objective results are unavailable.");
}

auto availableMeasurements(const analysis::AnalysisResult& result,
    const analysis::OptimizationObjectiveSpec& objective,
    const analysis::OptimizationObjectiveFailure& failure)
    -> std::optional<analysis::OptimizationObjectiveResult>
{
    auto available = objective;
    using Kind = analysis::OptimizationObjectiveFailureKind;
    switch (failure.kind) {
    case Kind::MissingForwardDirection:
    case Kind::NullForwardGain:
        available.forwardGainWeight = 0.0;
        available.frontToBackWeight = 0.0;
        available.frontToRearWeight = 0.0;
        break;
    case Kind::MissingBackDirection:
    case Kind::NullBackGain:
        available.frontToBackWeight = 0.0;
        break;
    case Kind::MissingRearCoverage:
    case Kind::NullRearGain:
        available.frontToRearWeight = 0.0;
        break;
    default:
        return std::nullopt;
    }
    const auto remainingWeight = available.swrWeight + available.resistanceWeight
        + available.reactanceWeight + available.forwardGainWeight
        + available.frontToBackWeight + available.frontToRearWeight;
    if (remainingWeight <= 0.0) return std::nullopt;
    return analysis::evaluateOptimizationObjective(result, available);
}

}

CandidateEvaluator::CandidateEvaluator(QObject* parent)
    : QObject(parent)
{
    runner_ = new SolverProcessRunner(this);
    connect(runner_, &SolverProcessRunner::finished,
        this, [this](SolverProcessResult result) { finishProcess(std::move(result)); });
}

auto CandidateEvaluator::isRunning() const noexcept -> bool
{
    return running_;
}

void CandidateEvaluator::start(CandidateEvaluationRequest request)
{
    if (running_) return;
    request_ = std::move(request);
    running_ = true;
    elapsed_.restart();

    const QDir directory(request_.directory);
    if (!writeFile(directory.filePath(QStringLiteral("model.source.nec")),
            request_.authoredSource.toUtf8())) {
        finishLater(CandidateEvaluationStatus::FileError,
            tr("Could not write the authored candidate source."));
        return;
    }
    const auto resolution = nec::NecSymbolResolver{}.resolve(
        request_.authoredSource.toStdString(), request_.variableValues);
    if (!resolution.ok()) {
        finishLater(CandidateEvaluationStatus::ExpressionError,
            tr("Candidate SY expressions could not be resolved."));
        return;
    }
    const auto checked = nec::NecModelChecker{}.check(
        nec::NecParser{}.parse(resolution.generatedDeck),
        {nec::compatibilityProfileForBackend(request_.backend.toStdString()),
            resolution.definitions.size()});
    if (checked.errorCount() != 0) {
        finishLater(CandidateEvaluationStatus::InvalidModel,
            tr("Candidate model failed NEC validation."));
        return;
    }

    auto frequencyPlan = request_.frequencyPlan;
    if (request_.objective.kind == analysis::OptimizationObjectiveKind::SwrAtFrequency) {
        frequencyPlan = {analysis::FrequencyPlanMode::Explicit,
            {request_.objective.targetFrequencyMHz}, {}};
    }
    const auto frequenciesMHz = analysis::frequencyPlanPoints(frequencyPlan);
    const auto directionalObjective = request_.objective.forwardGainWeight > 0.0
        || request_.objective.frontToBackWeight > 0.0
        || request_.objective.frontToRearWeight > 0.0;
    const auto solverDeck = directionalObjective
        ? request_.objective.directionalEvaluationMode
                == analysis::DirectionalEvaluationMode::SpecifiedDirection
            ? analysis::prepareDirectionalOptimizationInput(resolution.generatedDeck,
                  frequenciesMHz,
                  request_.objective.forwardThetaDegrees,
                  request_.objective.forwardPhiDegrees,
                  request_.objective.frontToBackWeight > 0.0,
                  request_.objective.frontToRearWeight > 0.0)
            : analysis::prepareDirectionalCutOptimizationInput(
                  resolution.generatedDeck, frequenciesMHz,
                  request_.objective.directionalEvaluationMode,
                  request_.objective.directionalEvaluationMode
                          == analysis::DirectionalEvaluationMode::StrongestInAzimuthCut
                      ? request_.objective.forwardThetaDegrees
                      : request_.objective.forwardPhiDegrees,
                  request_.objective.frontToBackWeight > 0.0,
                  request_.objective.frontToRearWeight > 0.0)
        : frequencyPlan.mode == analysis::FrequencyPlanMode::Explicit
            ? analysis::prepareExplicitFrequencyInput(
                  resolution.generatedDeck, frequenciesMHz)
            : analysis::prepareImpedanceInput(resolution.generatedDeck);
    if (!writeFile(directory.filePath(QStringLiteral("model.nec")),
            QByteArray::fromStdString(solverDeck))) {
        finishLater(CandidateEvaluationStatus::FileError,
            tr("Could not write candidate model files."));
        return;
    }

    analysis::SolverCommand command;
    try {
        command = analysis::buildSolverCommand(request_.backend.toStdString(),
            request_.executable.toStdString(), "model.nec", "model.out");
    } catch (const std::exception& error) {
        finishLater(CandidateEvaluationStatus::CommandError,
            QString::fromLocal8Bit(error.what()));
        return;
    }

    runner_->start(command, request_.directory, request_.timeoutSeconds);
}

void CandidateEvaluator::cancel()
{
    if (!running_) return;
    runner_->cancel();
}

void CandidateEvaluator::cancelAndWait()
{
    if (!running_) return;
    if (runner_->isRunning()) runner_->cancelAndWait();
    else finish(CandidateEvaluationStatus::Canceled);
}

void CandidateEvaluator::finishProcess(SolverProcessResult result)
{
    if (!result.output.isEmpty()) {
        writeFile(QDir(request_.directory).filePath(QStringLiteral("solver.log")),
            result.output);
    }
    switch (result.status) {
    case SolverProcessStatus::Succeeded:
        finish(CandidateEvaluationStatus::Completed);
        break;
    case SolverProcessStatus::FailedToStart:
        finish(CandidateEvaluationStatus::FailedToStart, std::move(result.detail));
        break;
    case SolverProcessStatus::Failed:
        if (const auto summary = solverOutputSummary(result.output); !summary.isEmpty()) {
            finish(CandidateEvaluationStatus::SolverFailed,
                tr("Solver exited with code %1: %2").arg(result.exitCode).arg(summary));
        } else {
            finish(CandidateEvaluationStatus::SolverFailed,
                tr("Solver exited with code %1.").arg(result.exitCode));
        }
        break;
    case SolverProcessStatus::TimedOut:
        finish(CandidateEvaluationStatus::TimedOut);
        break;
    case SolverProcessStatus::Canceled:
        finish(CandidateEvaluationStatus::Canceled);
        break;
    }
}

void CandidateEvaluator::finish(CandidateEvaluationStatus status, QString detail)
{
    if (!running_) return;
    CandidateEvaluationResult result;
    result.status = status;
    result.detail = std::move(detail);
    result.durationSeconds = elapsed_.elapsed() / 1000.0;
    QFile outputFile(QDir(request_.directory).filePath(QStringLiteral("model.out")));
    QByteArray output;
    if (outputFile.open(QIODevice::ReadOnly)) output = outputFile.readAll();
    result.outputBytes = output.size();

    if (status == CandidateEvaluationStatus::Completed) {
        result.analysis = analysis::NecOutputParser{}.parse(output.toStdString());
        std::set<double> frequencies;
        for (const auto& feedpoint : result.analysis.feedpoints)
            frequencies.insert(feedpoint.frequencyMHz);
        for (const auto& sample : result.analysis.radiation)
            frequencies.insert(sample.frequencyMHz);
        result.frequencyCount = static_cast<int>(frequencies.size());
        result.objective = analysis::evaluateOptimizationObjective(
            result.analysis, request_.objective);
        if (!result.objective) {
            const auto failure = analysis::diagnoseOptimizationObjectiveFailure(
                result.analysis, request_.objective);
            result.availableMeasurements = availableMeasurements(
                result.analysis, request_.objective, failure);
            result.status = CandidateEvaluationStatus::ObjectiveUnavailable;
            result.detail = objectiveFailureDetail(failure, request_.objective);
        }
    }

    running_ = false;
    emit finished(std::move(result));
}

void CandidateEvaluator::finishLater(CandidateEvaluationStatus status, QString detail)
{
    QTimer::singleShot(0, this, [this, status, detail = std::move(detail)]() mutable {
        finish(status, std::move(detail));
    });
}

}
