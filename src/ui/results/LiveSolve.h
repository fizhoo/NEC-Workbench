#pragma once

#include "analysis/AnalysisResult.h"
#include "model/ModelSetup.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <string>

class QTimer;

namespace necwb::ui {

// Solves the model at one frequency after a variable changes. It works on a copy of the solver
// deck with FR reduced to that frequency and RP kept, in a temporary folder: the user's deck
// and the Runs history are never touched. Only the latest solve counts; starting one cancels
// any still running.
class LiveSolve final : public QObject {
public:
    struct Solver {
        QString backendId;
        QString executable;
        int timeoutSeconds{};
    };

    explicit LiveSolve(QObject* parent = nullptr);
    ~LiveSolve() override;

    // generatedDeck is the resolved solver deck, as a full run would send it.
    void start(const std::string& generatedDeck, double frequencyMHz, const Solver& solver);
    void cancel();
    [[nodiscard]] auto isRunning() const -> bool;
    // How long the last completed solve took, in seconds.
    [[nodiscard]] auto lastSeconds() const -> double { return lastSeconds_; }

    // Called with the result of the latest solve.
    void setFinishedCallback(std::function<void(const analysis::AnalysisResult&, double)> callback);
    // Called with a message for the status bar: a failure, or a solve still running after 2 s.
    void setStatusCallback(std::function<void(const QString&)> callback);

    // The sweep's point nearest the middle of its band.
    [[nodiscard]] static auto bandCentreMHz(const model::FrequencyDefinition& frequency) -> double;

private:
    struct Job;
    void finish(Job& job, int status, const QString& detail);

    std::shared_ptr<Job> job_;
    QTimer* slow_{};
    QElapsedTimer elapsed_;
    double lastSeconds_{};
    std::function<void(const analysis::AnalysisResult&, double)> finished_;
    std::function<void(const QString&)> status_;
};

}
