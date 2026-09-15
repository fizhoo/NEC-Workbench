#pragma once

#include "ui/analysis/AnalysisRunStore.h"

#include <QDialog>

#include <functional>

class QCloseEvent;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QWidget;

namespace necwb::ui {

class AverageGainResultsView;
class ConvergenceWorkspace;
class CurrentDistributionView;
class ImpedanceResultsView;
class OptimizationWorkspace;
class Radiation3DView;
class RadiationPatternView;
class ResultsSummaryView;
class SweepPlotsView;

class RunReviewWindow final : public QDialog {
public:
    explicit RunReviewWindow(QWidget* parent = nullptr);

    void setOpenSnapshotCallback(
        std::function<void(const QString&, const QString&, const QString&)> callback);
    auto showRun(const AnalysisRunRecord& record, const QString& activeModelName) -> bool;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    auto showAnalysisRun(const AnalysisRunRecord& record, const QString& context) -> bool;
    auto showAverageGainRun(const AnalysisRunRecord& record, const QString& context) -> bool;
    auto showOptimizationSession(const AnalysisRunRecord& record) -> bool;
    auto showConvergenceSession(const AnalysisRunRecord& record) -> bool;
    void setInputSnapshot(const QString& authoredSource, const QString& generatedDeck);
    void refreshInputSnapshot();
    void refreshFrequency();
    void present();

    QLabel* identityLabel_{};
    QLabel* activeModelLabel_{};
    QLabel* statusLabel_{};
    QPushButton* openSnapshotButton_{};
    QComboBox* frequencyControl_{};
    QWidget* frequencyBar_{};
    QTabWidget* tabs_{};
    ResultsSummaryView* summaryView_{};
    QTabWidget* impedancePage_{};
    ImpedanceResultsView* impedanceView_{};
    SweepPlotsView* sweepPlotsView_{};
    CurrentDistributionView* currentsView_{};
    QTabWidget* radiationPage_{};
    RadiationPatternView* radiationPatternView_{};
    Radiation3DView* radiation3DView_{};
    QPlainTextEdit* rawOutput_{};
    QWidget* inputPage_{};
    QComboBox* inputSource_{};
    QPlainTextEdit* inputText_{};
    AverageGainResultsView* averageGainView_{};
    OptimizationWorkspace* optimizationView_{};
    ConvergenceWorkspace* convergenceView_{};
    AnalysisRunRecord record_;
    QString authoredSource_;
    QString generatedDeck_;
    std::function<void(const QString&, const QString&, const QString&)> openSnapshotCallback_;
};

}
