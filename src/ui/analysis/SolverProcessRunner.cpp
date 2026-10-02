#include "ui/analysis/SolverProcessRunner.h"

#include <QDateTime>
#include <QProcess>
#include <QTimer>

#include <algorithm>

namespace necwb::ui {

SolverProcessRunner::SolverProcessRunner(QObject* parent)
    : QObject(parent)
{
}

auto SolverProcessRunner::isRunning() const noexcept -> bool
{
    return process_ != nullptr;
}

void SolverProcessRunner::start(const analysis::SolverCommand& command,
    const QString& workingDirectory, int timeoutSeconds)
{
    if (isRunning()) return;

    output_.clear();
    cancelRequested_ = false;
    timeoutReached_ = false;
    startedAtMilliseconds_ = QDateTime::currentMSecsSinceEpoch();
    process_ = new QProcess(this);
    process_->setWorkingDirectory(workingDirectory);
    process_->setProgram(QString::fromStdString(command.executable));
    QStringList arguments;
    for (const auto& argument : command.arguments)
        arguments.append(QString::fromStdString(argument));
    process_->setArguments(arguments);
    process_->setProcessChannelMode(QProcess::MergedChannels);

    const auto standardInput = QByteArray::fromStdString(command.standardInput);
    connect(process_, &QProcess::started, this, [this, standardInput] {
        emit started();
        if (!standardInput.isEmpty()) process_->write(standardInput);
        process_->closeWriteChannel();
    });
    connect(process_, &QProcess::readyRead, this, [this] { drainOutput(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && isRunning())
            complete(SolverProcessStatus::FailedToStart, -1, process_->errorString());
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            if (!isRunning()) return;
            const auto status = cancelRequested_ ? SolverProcessStatus::Canceled
                : timeoutReached_ ? SolverProcessStatus::TimedOut
                : exitStatus == QProcess::NormalExit && exitCode == 0
                    ? SolverProcessStatus::Succeeded : SolverProcessStatus::Failed;
            complete(status, exitCode);
        });

    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this] {
        if (!isRunning()) return;
        timeoutReached_ = true;
        process_->terminate();
        stopAfterGracePeriod();
    });
    timeout_->start(std::max(1, timeoutSeconds) * 1000);
    process_->start();
}

void SolverProcessRunner::cancel()
{
    if (!isRunning()) return;
    cancelRequested_ = true;
    process_->terminate();
    stopAfterGracePeriod();
}

void SolverProcessRunner::cancelAndWait()
{
    if (!isRunning()) return;
    cancelRequested_ = true;
    auto* process = process_;
    process->kill();
    process->waitForFinished(2000);
    if (process_ == process) complete(SolverProcessStatus::Canceled);
}

void SolverProcessRunner::stopAfterGracePeriod()
{
    auto* process = process_;
    QTimer::singleShot(2000, this, [this, process] {
        if (process_ == process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void SolverProcessRunner::drainOutput()
{
    if (!isRunning()) return;
    const auto output = process_->readAll();
    if (output.isEmpty()) return;
    output_.append(output);
    emit outputReady(output);
}

void SolverProcessRunner::complete(SolverProcessStatus status, int exitCode, QString detail)
{
    if (!isRunning()) return;
    drainOutput();
    if (timeout_ != nullptr) {
        timeout_->stop();
        timeout_->deleteLater();
        timeout_ = nullptr;
    }
    auto* process = process_;
    process_ = nullptr;
    process->deleteLater();
    const auto durationSeconds = (QDateTime::currentMSecsSinceEpoch()
        - startedAtMilliseconds_) / 1000.0;
    emit finished({status, exitCode, std::move(detail), output_, durationSeconds});
}

}
