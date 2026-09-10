#pragma once

#include "analysis/SolverCommand.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QProcess;
class QTimer;

namespace necwb::ui {

enum class SolverProcessStatus {
    Succeeded,
    FailedToStart,
    Failed,
    TimedOut,
    Canceled
};

struct SolverProcessResult {
    SolverProcessStatus status{SolverProcessStatus::Failed};
    int exitCode{-1};
    QString detail;
    QByteArray output;
    double durationSeconds{};
};

class SolverProcessRunner final : public QObject {
    Q_OBJECT

public:
    explicit SolverProcessRunner(QObject* parent = nullptr);

    [[nodiscard]] auto isRunning() const noexcept -> bool;
    void start(const analysis::SolverCommand& command, const QString& workingDirectory,
        int timeoutSeconds);
    void cancel();
    void cancelAndWait();

signals:
    void started();
    void outputReady(QByteArray output);
    void finished(necwb::ui::SolverProcessResult result);

private:
    void stopAfterGracePeriod();
    void drainOutput();
    void complete(SolverProcessStatus status, int exitCode = -1, QString detail = {});

    QProcess* process_{};
    QTimer* timeout_{};
    QByteArray output_;
    qint64 startedAtMilliseconds_{};
    bool cancelRequested_{};
    bool timeoutReached_{};
};

}

