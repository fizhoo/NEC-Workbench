#pragma once

#include "analysis/AnalysisResult.h"

#include <QWidget>

class QLabel;
class QComboBox;
class QPlainTextEdit;
class QWidget;

namespace necwb::ui {

class ResultsSummaryView final : public QWidget {
public:
    explicit ResultsSummaryView(QWidget* parent = nullptr);

    void setResults(const analysis::AnalysisResult& result, const QString& context);
    void setSelectedFrequency(double frequencyMHz);
    void setHistoricalInputSnapshot(const QString& authoredSource,
        const QString& generatedDeck);
    void clearInputSnapshot();
    void clear();

private:
    void refresh();
    void refreshInputSnapshot();

    analysis::AnalysisResult result_;
    QString context_;
    double selectedFrequency_{};
    QLabel* contextLabel_{};
    QLabel* frequencyLabel_{};
    QLabel* impedanceLabel_{};
    QLabel* swrLabel_{};
    QLabel* radiationLabel_{};
    QLabel* availabilityLabel_{};
    QWidget* inputSnapshotPanel_{};
    QComboBox* inputSnapshotSource_{};
    QPlainTextEdit* inputSnapshotText_{};
    QString authoredSource_;
    QString generatedDeck_;
};

}
