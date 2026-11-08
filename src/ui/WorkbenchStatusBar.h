#pragma once

#include <QStatusBar>

#include <cstddef>
#include <optional>
#include <vector>

class QLabel;
class QToolButton;

namespace necwb::ui {

// One-line status: the model file on the left; backend, loaded result frequencies, run state,
// and model check counts on the right. Temporary messages still use showMessage().
class WorkbenchStatusBar final : public QStatusBar {
    Q_OBJECT

public:
    explicit WorkbenchStatusBar(QWidget* parent = nullptr);

    void setFileStatus(const QString& text, const QString& toolTip);
    void setBackend(const QString& backend);
    // Shows the selected frequency with the range of the loaded results.
    void setResultFrequencies(const std::vector<double>& frequenciesMHz,
        std::optional<double> selectedMHz);
    void setRunning(double elapsedSeconds);
    void setRunFinished(const QString& status, bool succeeded, double elapsedSeconds);
    void setCheckCounts(std::size_t errors, std::size_t warnings, const QString& detail);
    void setCheckRequired();

signals:
    void problemsRequested();

private:
    QLabel* file_{};
    QLabel* backend_{};
    QLabel* frequency_{};
    QLabel* runState_{};
    QToolButton* counts_{};
};

}
