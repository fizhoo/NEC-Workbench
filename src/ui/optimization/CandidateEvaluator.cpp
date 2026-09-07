#include "ui/optimization/CandidateEvaluator.h"

#include "analysis/NecOutputParser.h"
#include "analysis/SolverCommand.h"
#include "analysis/SolverInput.h"
#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"
#include "nec/NecSymbolResolver.h"

#include <QDir>
#include <QFile>
#include <QProcess>
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
    canceled_ = false;
    timedOut_ = false;
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

    const auto frequenciesMHz = analysis::frequencyPlanPoints(request_.frequencyPlan);
    const auto solverDeck = request_.frequencyPlan.mode == analysis::FrequencyPlanMode::Explicit
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

    process_ = new QProcess(this);
    auto* process = process_;
    process->setWorkingDirectory(request_.directory);
    process->setProgram(QString::fromStdString(command.executable));
    QStringList arguments;
    for (const auto& argument : command.arguments)
        arguments.append(QString::fromStdString(argument));
    process->setArguments(arguments);
    connect(process, &QProcess::errorOccurred, this,
        [this, process](QProcess::ProcessError error) {
            if (running_ && process_ == process && error == QProcess::FailedToStart)
                finish(CandidateEvaluationStatus::FailedToStart, process->errorString());
        });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process](int exitCode, QProcess::ExitStatus status) {
            if (!running_ || process_ != process) return;
            if (canceled_) finish(CandidateEvaluationStatus::Canceled);
            else if (timedOut_) finish(CandidateEvaluationStatus::TimedOut);
            else if (status == QProcess::NormalExit && exitCode == 0)
                finish(CandidateEvaluationStatus::Completed);
            else
                finish(CandidateEvaluationStatus::SolverFailed,
                    tr("Solver exited with code %1.").arg(exitCode));
        });

    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this, process] {
        if (running_ && process_ == process) {
            timedOut_ = true;
            process->kill();
        }
    });
    timeout_->start(std::max(1, request_.timeoutSeconds) * 1000);
    process->start();
}

void CandidateEvaluator::cancel()
{
    if (!running_) return;
    canceled_ = true;
    if (process_ != nullptr) process_->terminate();
}

void CandidateEvaluator::cancelAndWait()
{
    if (!running_) return;
    canceled_ = true;
    if (process_ != nullptr) {
        auto* process = process_;
        process->kill();
        process->waitForFinished(2000);
        if (running_) finish(CandidateEvaluationStatus::Canceled);
    } else {
        finish(CandidateEvaluationStatus::Canceled);
    }
}

void CandidateEvaluator::finish(CandidateEvaluationStatus status, QString detail)
{
    if (!running_) return;
    if (timeout_ != nullptr) {
        timeout_->stop();
        timeout_->deleteLater();
        timeout_ = nullptr;
    }

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
            result.analysis.feedpoints, request_.objective);
        if (!result.objective || !result.objective->feedpoint)
            result.status = CandidateEvaluationStatus::NoImpedance;
    }

    if (process_ != nullptr) {
        process_->deleteLater();
        process_ = nullptr;
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
