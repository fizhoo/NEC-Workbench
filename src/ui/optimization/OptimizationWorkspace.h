#pragma once

#include "nec/NecSymbolResolver.h"
#include "ui/analysis/AnalysisRunStore.h"

#include <QElapsedTimer>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace necwb::ui {

class OptimizationWorkspace final : public QWidget {
public:
    explicit OptimizationWorkspace(QWidget* parent = nullptr);

    void setContext(QString source, QString sourceFile, QString backend,
        QString executable, int timeoutSeconds, bool modelValid);
    void setModelValid(bool valid);
    void setExternalRunActive(bool active);
    void setRunsChangedCallback(std::function<void()> callback);
    void setRunningChangedCallback(std::function<void()> callback);
    [[nodiscard]] auto isRunning() const noexcept -> bool;
    void cancelAndWait();

private:
    struct Candidate {
        double value{};
        int row{};
        AnalysisRunRecord record;
    };

    void populateVariables(const nec::SymbolResolution& resolution);
    void updateBounds();
    void updateReadiness();
    void startSweep();
    void cancelSweep();
    void startNextCandidate();
    void finishCurrentCandidate(bool processSucceeded, const QString& detail = {});
    void finishSweep();
    auto writeCandidateFiles(Candidate& candidate, const std::string& generatedDeck) -> bool;
    void setCandidateStatus(int row, const QString& status);

    QTableWidget* variablesTable_{};
    QComboBox* variableControl_{};
    QDoubleSpinBox* minimumControl_{};
    QDoubleSpinBox* maximumControl_{};
    QSpinBox* pointsControl_{};
    QDoubleSpinBox* referenceImpedanceControl_{};
    QPushButton* runButton_{};
    QPushButton* cancelButton_{};
    QProgressBar* progress_{};
    QLabel* statusLabel_{};
    QLabel* bestLabel_{};
    QTableWidget* resultsTable_{};
    QProcess* process_{};
    QTimer* timeout_{};
    QElapsedTimer elapsed_;
    AnalysisRunStore runStore_;
    std::function<void()> runsChangedCallback_;
    std::function<void()> runningChangedCallback_;
    std::vector<nec::SymbolDefinition> definitions_;
    std::vector<Candidate> candidates_;
    QString source_;
    QString sourceFile_;
    QString backend_;
    QString executable_;
    QString selectedSymbol_;
    int timeoutSeconds_{120};
    std::size_t candidateIndex_{};
    double bestScore_{};
    int bestRow_{-1};
    bool modelValid_{};
    bool externalRunActive_{};
    bool cancelRequested_{};
    bool timedOut_{};
};

}
