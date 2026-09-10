#pragma once

#include "analysis/AnalysisResult.h"
#include "analysis/FrequencyPlan.h"
#include "analysis/OptimizationObjective.h"
#include "ui/analysis/SolverProcessRunner.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace necwb::ui {

enum class CandidateEvaluationStatus {
    Completed,
    ExpressionError,
    InvalidModel,
    FileError,
    CommandError,
    FailedToStart,
    SolverFailed,
    TimedOut,
    Canceled,
    NoImpedance
};

struct CandidateEvaluationRequest {
    QString authoredSource;
    std::unordered_map<std::string, double> variableValues;
    analysis::FrequencyPlan frequencyPlan;
    analysis::OptimizationObjectiveSpec objective;
    QString backend;
    QString executable;
    QString directory;
    int timeoutSeconds{120};
};

struct CandidateEvaluationResult {
    CandidateEvaluationStatus status{CandidateEvaluationStatus::SolverFailed};
    QString detail;
    analysis::AnalysisResult analysis;
    std::optional<analysis::OptimizationObjectiveResult> objective;
    double durationSeconds{};
    qint64 outputBytes{};
    int frequencyCount{};
};

class CandidateEvaluator final : public QObject {
    Q_OBJECT

public:
    explicit CandidateEvaluator(QObject* parent = nullptr);

    [[nodiscard]] auto isRunning() const noexcept -> bool;
    void start(CandidateEvaluationRequest request);
    void cancel();
    void cancelAndWait();

signals:
    void finished(necwb::ui::CandidateEvaluationResult result);

private:
    void finish(CandidateEvaluationStatus status, QString detail = {});
    void finishLater(CandidateEvaluationStatus status, QString detail = {});
    void finishProcess(SolverProcessResult result);

    CandidateEvaluationRequest request_;
    SolverProcessRunner* runner_{};
    QElapsedTimer elapsed_;
    bool running_{};
};

}
