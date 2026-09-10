#pragma once

#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/SolverProcessRunner.h"

#include <QString>
#include <QWidget>

#include <complex>
#include <functional>
#include <optional>
#include <vector>

class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace necwb::ui {

class ConvergenceWorkspace final : public QWidget {
public:
    explicit ConvergenceWorkspace(QWidget* parent = nullptr);

    void setContext(QString source, QString sourceFile, QString backend,
        QString executable, int timeoutSeconds, bool modelValid);
    void setExternalRunActive(bool active);
    void setRunsChangedCallback(std::function<void()> callback);
    void setRunningChangedCallback(std::function<void()> callback);
    void setSummaryChangedCallback(std::function<void(const QString&)> callback);
    void setReturnToCurrentWorkCallback(std::function<void()> callback);
    [[nodiscard]] auto isRunning() const noexcept -> bool;
    auto loadSession(const QString& sessionId) -> bool;
    void leaveHistoricalSession();
    void cancel();
    void cancelAndWait();
    void clearResults();
    void markStale();

private:
    struct Step {
        double scale{1.0};
        int totalSegments{};
        int row{};
        AnalysisRunRecord record;
        QString warning;
        std::optional<std::complex<double>> impedance;
        std::optional<double> peakGainDb;
        bool withinTolerance{};
    };

    void updateReadiness();
    void startStudy();
    void cancelStudy();
    void startNextStep();
    void finishCurrentStep(SolverProcessResult result);
    void finishStudy();
    auto writeStepFiles(Step& step, const std::string& deck) -> bool;
    void populateStepResult(Step& step, const QByteArray& output);
    void setStepStatus(int row, const QString& status);
    void resetTable(int rows);

    QDoubleSpinBox* frequencyControl_{};
    QSpinBox* increaseControl_{};
    QSpinBox* levelsControl_{};
    QDoubleSpinBox* impedanceToleranceControl_{};
    QDoubleSpinBox* gainToleranceControl_{};
    QPushButton* runButton_{};
    QPushButton* cancelButton_{};
    QProgressBar* progress_{};
    QLabel* statusLabel_{};
    QLabel* conclusionLabel_{};
    QTableWidget* resultsTable_{};
    QWidget* historicalBanner_{};
    QLabel* historicalBannerTitle_{};
    QPushButton* returnToCurrentWorkButton_{};
    SolverProcessRunner* runner_{};
    AnalysisRunStore runStore_;
    std::function<void()> runsChangedCallback_;
    std::function<void()> runningChangedCallback_;
    std::function<void(const QString&)> summaryChangedCallback_;
    std::function<void()> returnToCurrentWorkCallback_;
    std::vector<Step> steps_;
    std::optional<AnalysisRunRecord> sessionRecord_;
    QString source_;
    QString numericSource_;
    QString sourceFile_;
    QString backend_;
    QString executable_;
    int timeoutSeconds_{120};
    std::size_t stepIndex_{};
    bool modelValid_{};
    bool externalRunActive_{};
    bool cancelRequested_{};
    bool hasResults_{};
    bool trackGain_{};
    bool modelChangedDuringRun_{};
    bool historicalSession_{};
};

}
