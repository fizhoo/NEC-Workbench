#include "ui/results/LiveSolve.h"

#include "analysis/NecOutputParser.h"
#include "analysis/SolverCommand.h"
#include "analysis/SolverInput.h"
#include "ui/DisplayFormat.h"
#include "ui/analysis/SolverProcessRunner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <exception>

namespace necwb::ui {
namespace {

constexpr auto SlowAfterMs = 2000;

}

struct LiveSolve::Job {
    QTemporaryDir directory;
    SolverProcessRunner* runner{};
    double frequencyMHz{};
};

LiveSolve::LiveSolve(QObject* parent) : QObject(parent)
{
    slow_ = new QTimer(this);
    slow_->setInterval(500);
    connect(slow_, &QTimer::timeout, this, [this] {
        if (!job_ || elapsed_.elapsed() < SlowAfterMs || !status_) return;
        status_(tr("Live run at %1 MHz… %2 s").arg(formatDecimal(job_->frequencyMHz))
            .arg(static_cast<double>(elapsed_.elapsed()) / 1000.0, 0, 'f', 1));
    });
}

LiveSolve::~LiveSolve()
{
    if (job_ && job_->runner->isRunning()) job_->runner->cancelAndWait();
}

void LiveSolve::start(const std::string& generatedDeck, double frequencyMHz, const Solver& solver)
{
    cancel();
    auto job = std::make_shared<Job>();
    job->frequencyMHz = frequencyMHz;
    model::FrequencyDefinition point;
    point.startMHz = frequencyMHz;
    point.count = 1;
    const auto input = analysis::formatSolverInputForBackend(
        analysis::prepareFrequencySweepInput(generatedDeck, point, true),
        solver.backendId.toStdString());
    QFile file(QDir(job->directory.path()).filePath(QStringLiteral("model.nec")));
    analysis::SolverCommand command;
    try {
        command = analysis::buildSolverCommand(solver.backendId.toStdString(),
            solver.executable.toStdString(), "model.nec", "model.out");
    } catch (const std::exception& error) {
        if (status_) status_(tr("Live run skipped: %1").arg(QString::fromLocal8Bit(error.what())));
        return;
    }
    if (!job->directory.isValid() || !file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (status_) status_(tr("Live run skipped: no temporary folder"));
        return;
    }
    file.write(QByteArray::fromStdString(input));
    file.close();
    job->runner = new SolverProcessRunner(this);
    connect(job->runner, &SolverProcessRunner::finished, this,
        [this, raw = job.get()](const SolverProcessResult& result) {
            finish(*raw, static_cast<int>(result.status), result.detail);
        });
    job_ = job;
    elapsed_.start();
    slow_->start();
    job->runner->start(command, job->directory.path(), solver.timeoutSeconds);
}

void LiveSolve::cancel()
{
    slow_->stop();
    if (!job_) return;
    // The canceled solve finishes on its own; keep its folder until then.
    auto job = std::move(job_);
    disconnect(job->runner, nullptr, this, nullptr);
    connect(job->runner, &SolverProcessRunner::finished, this, [job]() mutable {
        job->runner->deleteLater();
        job.reset();
    });
    job->runner->cancel();
}

auto LiveSolve::isRunning() const -> bool
{
    return job_ && job_->runner->isRunning();
}

void LiveSolve::setFinishedCallback(
    std::function<void(const analysis::AnalysisResult&, double)> callback)
{
    finished_ = std::move(callback);
}

void LiveSolve::setStatusCallback(std::function<void(const QString&)> callback)
{
    status_ = std::move(callback);
}

void LiveSolve::finish(Job& job, int status, const QString& detail)
{
    if (job_.get() != &job) return;
    slow_->stop();
    const auto keep = job_;
    job_.reset();
    job.runner->deleteLater();
    lastSeconds_ = static_cast<double>(elapsed_.elapsed()) / 1000.0;
    QFile output(QDir(job.directory.path()).filePath(QStringLiteral("model.out")));
    if (status != static_cast<int>(SolverProcessStatus::Succeeded)
        || !output.open(QIODevice::ReadOnly)) {
        if (status_) status_(tr("Live run at %1 MHz failed%2").arg(formatDecimal(job.frequencyMHz),
            detail.isEmpty() ? QString{} : tr(": %1").arg(detail)));
        return;
    }
    const auto bytes = output.readAll();
    const auto result = analysis::NecOutputParser{}.parse(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (status_) status_(tr("Live: %1 MHz solved in %2 s").arg(formatDecimal(job.frequencyMHz))
        .arg(lastSeconds_, 0, 'f', 2));
    if (finished_) finished_(result, job.frequencyMHz);
}

auto LiveSolve::bandCentreMHz(const model::FrequencyDefinition& frequency) -> double
{
    const auto end = model::frequencyEndMHz(frequency);
    const auto middle = (frequency.startMHz + end) / 2.0;
    auto best = frequency.startMHz;
    for (auto index = 0; index < frequency.count; ++index) {
        const auto value = model::frequencyAtIndex(frequency, index);
        if (std::abs(value - middle) < std::abs(best - middle)) best = value;
    }
    return best;
}

}
