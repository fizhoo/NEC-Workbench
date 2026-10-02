#include "ui/optimization/CandidateEvaluator.h"

#include "analysis/NecOutputParser.h"
#include "analysis/SolverCommand.h"
#include "analysis/SolverInput.h"
#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"
#include "nec/NecSymbolResolver.h"

#include <QDir>
#include <QFile>
#include <QTimer>

#include <algorithm>
#include <exception>
#include <set>
#include <utility>

namespace necwb::ui {
namespace {

auto writeFile(const QString& path, const QByteArray& data) -> bool
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(data) == data.size();
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
        nec::NecParser{}.parse(resolution.generatedDeck));
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
        ? analysis::prepareDirectionalOptimizationInput(resolution.generatedDeck,
              frequenciesMHz,
              request_.objective.forwardThetaDegrees,
              request_.objective.forwardPhiDegrees,
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
    switch (result.status) {
    case SolverProcessStatus::Succeeded:
        finish(CandidateEvaluationStatus::Completed);
        break;
    case SolverProcessStatus::FailedToStart:
        finish(CandidateEvaluationStatus::FailedToStart, std::move(result.detail));
        break;
    case SolverProcessStatus::Failed:
        finish(CandidateEvaluationStatus::SolverFailed,
            tr("Solver exited with code %1.").arg(result.exitCode));
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
        result.frequencyCount = static_cast<int>(frequencies.size());
        result.objective = analysis::evaluateOptimizationObjective(
            result.analysis, request_.objective);
        if (!result.objective)
            result.status = CandidateEvaluationStatus::NoImpedance;
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
