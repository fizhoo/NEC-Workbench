#include "analysis/AnalysisResult.h"
#include "ui/DisplayFormat.h"
#include "ui/DetachablePanel.h"
#include "ui/WorkspaceNavigator.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/analysis/AnalysisSetupEditor.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/ResultsSummaryView.h"
#include "ui/analysis/RunReviewWindow.h"
#include "ui/analysis/AverageGainResultsView.h"
#include "ui/analysis/AnalysisRequestEditor.h"
#include "ui/analysis/ConvergenceWorkspace.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/dashboard/DashboardPage.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/cards/WireCardEditor.h"
#include "ui/editor/NecEditor.h"
#include "ui/geometry/WirePropertiesDialog.h"
#include "ui/geometry/AutoSegmentationDialog.h"
#include "ui/geometry/GeometryView.h"
#include "ui/geometry/Geometry3DView.h"
#include "ui/optimization/OptimizationWorkspace.h"
#include "ui/model/ParameterEditor.h"
#include "ui/setup/LoadNetworkEditor.h"
#include "ui/setup/SetupEditor.h"
#include "ui/welcome/WelcomePage.h"
#include "nec/NecParser.h"
#include "model/WireGauge.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextCursor>
#include <QTextDocument>
#include <QTableWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QPushButton>
#include <QWheelEvent>
#include <QShortcut>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

auto smokeFailure(const char* checkpoint, int line) -> int
{
    std::fprintf(stderr, "Plot smoke failure at line %d: %s\n", line, checkpoint);
    std::fflush(stderr);
    return EXIT_FAILURE;
}

}

#define NECWB_SMOKE_FAILURE(checkpoint) smokeFailure(checkpoint, __LINE__)

auto main(int argc, char* argv[]) -> int
{
    if (argc == 3 && QByteArray(argv[1]).startsWith("-i")
        && QByteArray(argv[2]).startsWith("-o")) {
        QFile output(QString::fromLocal8Bit(argv[2] + 2));
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return EXIT_FAILURE;
        const QByteArray solverOutput =
            " FREQUENCY : 7.0000E+00 MHz\n"
            " --------- ANTENNA INPUT PARAMETERS ---------\n"
            " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n"
            " FREQUENCY : 1.4000E+01 MHz\n"
            " --------- ANTENNA INPUT PARAMETERS ---------\n"
            " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 7.5E+01 1.5E+01 0 0 5.0E-03\n";
        return output.write(solverOutput) == solverOutput.size() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    necwb::ui::AnalysisSetupEditor solverSetup;
    auto* solverBackend = solverSetup.findChild<QComboBox*>(
        QStringLiteral("analysisBackendControl"));
    auto* solverExecutable = solverSetup.findChild<QLineEdit*>(
        QStringLiteral("analysisExecutableControl"));
    const auto backendChoicesValid = solverBackend != nullptr
        && solverExecutable != nullptr && solverBackend->count() == 3
        && solverBackend->itemData(0).toString() == QStringLiteral("nec2")
        && solverBackend->itemData(1).toString() == QStringLiteral("opennec")
        && solverBackend->itemData(2).toString() == QStringLiteral("nec2dxs");
    const QHash<QString, QString> solverPaths{
        {QStringLiteral("nec2"), QCoreApplication::applicationFilePath()},
        {QStringLiteral("opennec"), QStringLiteral("/tmp/onec")},
        {QStringLiteral("nec2dxs"), QStringLiteral("/tmp/Nec2dXS11k.exe")},
    };
    solverSetup.setSettings(QStringLiteral("nec2"), solverPaths, 120);
    if (solverBackend != nullptr) solverBackend->setCurrentIndex(2);
    const auto backendChangeRestoresExecutable = solverExecutable != nullptr
        && solverExecutable->text() == QStringLiteral("/tmp/Nec2dXS11k.exe");
    if (solverBackend != nullptr) solverBackend->setCurrentIndex(0);
    const auto backendReturnRestoresExecutable = solverExecutable != nullptr
        && solverExecutable->text() == QCoreApplication::applicationFilePath();
    if (!backendChoicesValid || !backendChangeRestoresExecutable
        || !backendReturnRestoresExecutable)
        return NECWB_SMOKE_FAILURE("solver backend selection");
    necwb::ui::NecEditor historyEditor;
    const auto originalSource = QStringLiteral("CM original\nCE\nGE 0\nEN");
    const auto typedSource = QStringLiteral("CM typed\nCM original\nCE\nGE 0\nEN");
    const auto structuredSource = QStringLiteral(
        "CM typed\nCM original\nCE\nGW 1 11 0 0 0 1 0 0 0.001\nGE 0\nEN");
    historyEditor.setPlainText(originalSource);
    historyEditor.moveCursor(QTextCursor::Start);
    historyEditor.insertPlainText(QStringLiteral("CM typed\n"));
    historyEditor.replaceTextAsSingleEdit(structuredSource);
    const auto combinedHistoryRecorded = historyEditor.toPlainText() == structuredSource
        && historyEditor.document()->isUndoAvailable();
    historyEditor.undo();
    const auto structuredUndoRestoresTyping = historyEditor.toPlainText() == typedSource;
    historyEditor.undo();
    const auto typingUndoRestoresOriginal = historyEditor.toPlainText() == originalSource;
    historyEditor.redo();
    historyEditor.redo();
    const auto combinedHistoryRedoes = historyEditor.toPlainText() == structuredSource;
    if (!combinedHistoryRecorded || !structuredUndoRestoresTyping
        || !typingUndoRestoresOriginal || !combinedHistoryRedoes) {
        return NECWB_SMOKE_FAILURE("unified source undo history");
    }
    if (necwb::ui::formatDecimal(1.2) != QStringLiteral("1.200")
        || necwb::ui::formatDecimal(0.0004) != QStringLiteral("4.000e-04")) {
        return NECWB_SMOKE_FAILURE("display formatting");
    }
    QSettings{}.remove(QStringLiteral("workspaceNavigation/smoke/collapsed"));
    auto* navigationPages = new QTabWidget;
    navigationPages->addTab(new QWidget(navigationPages), QStringLiteral("First"));
    navigationPages->addTab(new QWidget(navigationPages), QStringLiteral("Second"));
    necwb::ui::WorkspaceNavigator workspaceNavigator(
        navigationPages, QStringLiteral("smoke"));
    workspaceNavigator.resize(600, 400);
    workspaceNavigator.show();
    application.processEvents();
    auto* workspaceNavigation = workspaceNavigator.findChild<QListWidget*>(
        QStringLiteral("smokeWorkspaceNavigation"));
    auto* workspaceCollapse = workspaceNavigator.findChild<QToolButton*>(
        QStringLiteral("smokeWorkspaceCollapseButton"));
    const auto navigatorInitialState = workspaceNavigation != nullptr
        && workspaceCollapse != nullptr && navigationPages->tabBar()->isHidden()
        && workspaceNavigation->count() == 2 && workspaceNavigation->currentRow() == 0;
    if (workspaceNavigation != nullptr) workspaceNavigation->setCurrentRow(1);
    const auto navigatorSelectsPage = navigationPages->currentIndex() == 1;
    navigationPages->setCurrentIndex(0);
    const auto pageSelectsNavigator = workspaceNavigation != nullptr
        && workspaceNavigation->currentRow() == 0;
    if (workspaceCollapse != nullptr) workspaceCollapse->click();
    const auto navigatorCollapses = workspaceNavigator.isCollapsed()
        && workspaceNavigation != nullptr && workspaceNavigation->item(0)->text().isEmpty();
    if (workspaceCollapse != nullptr) workspaceCollapse->click();
    const auto navigatorExpands = !workspaceNavigator.isCollapsed()
        && workspaceNavigation != nullptr
        && workspaceNavigation->item(0)->text() == QStringLiteral("First");
    QSettings{}.remove(QStringLiteral("workspaceNavigation/smoke/collapsed"));
    if (!navigatorInitialState || !navigatorSelectsPage || !pageSelectsNavigator
        || !navigatorCollapses || !navigatorExpands) {
        return NECWB_SMOKE_FAILURE("compact workspace navigation");
    }
    necwb::analysis::AnalysisResult result;
    result.feedpoints = {
        {14.0, 1, 6, {1.0, 0.0}, {0.01, 0.0}, {40.0, -12.0}, 0.005},
        {14.1, 1, 6, {1.0, 0.0}, {0.01, 0.0}, {50.0, 0.0}, 0.005},
        {14.2, 1, 6, {1.0, 0.0}, {0.01, 0.0}, {63.0, 18.0}, 0.005},
    };
    result.currents = {
        {14.1, 1, 1, 0, 0, -0.1, 0.05, {0.01, 0.0}, 0.01, 0.0},
        {14.1, 2, 1, 0, 0, 0.1, 0.05, {0.008, 0.002}, 0.00825, 14.0},
    };
    for (auto phi = 0; phi < 360; phi += 30) {
        for (auto theta = 0; theta <= 180; theta += 15) {
            const auto gain = 2.15 + 20.0 * std::log10(std::max(0.001,
                std::abs(std::sin(theta * 3.14159265358979323846 / 180.0))));
            result.radiation.push_back({14.1, static_cast<double>(theta),
                static_cast<double>(phi), gain, -999.99, gain});
            result.radiation.push_back({14.2, static_cast<double>(theta),
                static_cast<double>(phi), gain + 0.1, -999.99, gain + 0.1});
        }
    }
    result.radiation.push_back({14.2, 75.0, 60.0, 20.0, -999.99, 20.0});
    for (auto phi = 0; phi < 360; ++phi) {
        auto sample = necwb::analysis::RadiationSample{
            14.2, 62.0, static_cast<double>(phi), -10.0, -999.99, -10.0};
        sample.patternIndex = 1;
        result.radiation.push_back(sample);
    }
    QSettings plotSettings;
    const auto previousSwrScale = plotSettings.value(QStringLiteral("results/swrScaleModeV2"));
    plotSettings.setValue(QStringLiteral("results/swrScaleModeV2"), 0);
    plotSettings.sync();
    necwb::ui::SweepPlotsView view;
    view.resize(900, 700);
    view.setResults(result, QStringLiteral("test-run"));
    view.setSelectedFrequency(14.2);
    view.show();
    application.processEvents();

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    view.render(&painter);
    painter.end();
    auto* impedanceSweepPlot = view.findChild<QWidget*>(QStringLiteral("impedanceSweepPlot"));
    auto* swrSweepPlot = view.findChild<QWidget*>(QStringLiteral("swrSweepPlot"));
    auto* swrScaleControl = view.findChild<QComboBox*>(QStringLiteral("swrScaleControl"));
    const auto engineeringAxes = impedanceSweepPlot != nullptr && swrSweepPlot != nullptr
        && impedanceSweepPlot->property("dualAxes").toBool()
        && impedanceSweepPlot->property("leftAxisScale").toInt() == 1
        && impedanceSweepPlot->property("rightAxisScale").toInt() == 0
        && impedanceSweepPlot->property("leftAxisMinimum").toDouble() > 0.0
        && impedanceSweepPlot->property("rightAxisMinimum").toDouble() <= 0.0
        && impedanceSweepPlot->property("rightAxisMaximum").toDouble() >= 0.0
        && impedanceSweepPlot->property("leftAxisTickCount").toInt() >= 2
        && impedanceSweepPlot->property("rightAxisTickCount").toInt() >= 2
        && impedanceSweepPlot->property("plotLeftMargin").toDouble() >= 90.0
        && impedanceSweepPlot->property("plotRightMargin").toDouble() >= 90.0
        && !swrSweepPlot->property("dualAxes").toBool()
        && swrSweepPlot->property("leftAxisScale").toInt() == 1
        && swrSweepPlot->property("leftAxisMinimum").toDouble() == 1.0
        && swrSweepPlot->property("leftAxisTickCount").toInt() >= 3;
    if (swrScaleControl == nullptr || swrScaleControl->count() != 4
        || swrScaleControl->currentText() != QStringLiteral("Logarithmic")) {
        return NECWB_SMOKE_FAILURE("SWR scale control initialization");
    }
    swrScaleControl->setCurrentIndex(2);
    QPainter alternateScalePainter(&image);
    view.render(&alternateScalePainter);
    alternateScalePainter.end();
    const auto alternateScalesWork = swrSweepPlot->property("scaleMode").toInt() == 2
        && swrSweepPlot->property("leftAxisScale").toInt() == 0
        && swrSweepPlot->property("leftAxisMinimum").toDouble() == 1.0
        && swrSweepPlot->property("leftAxisMaximum").toDouble() == 3.0;
    if (previousSwrScale.isValid()) {
        plotSettings.setValue(QStringLiteral("results/swrScaleModeV2"), previousSwrScale);
    } else {
        plotSettings.remove(QStringLiteral("results/swrScaleModeV2"));
    }
    plotSettings.sync();
    if (!alternateScalesWork) return NECWB_SMOKE_FAILURE("alternate SWR scales");
    necwb::ui::CandidatePlotsView candidatePlots;
    candidatePlots.resize(760, 360);
    const std::vector<necwb::ui::CandidatePlotPoint> candidatePoints{
        {1, 10.0, {.score = 1.2, .swr = 1.5, .swrComponent = 0.9,
            .resistanceComponent = 0.2, .reactanceComponent = 0.1}},
        {2, 11.0, {.score = 1.6, .swr = 1.8, .swrComponent = 1.1,
            .resistanceComponent = 0.3, .reactanceComponent = 0.2}},
        {0, 9.0, {.score = 2.0, .swr = 2.5, .swrComponent = 1.5,
            .resistanceComponent = 0.3, .reactanceComponent = 0.2}},
    };
    candidatePlots.setCandidates(QStringLiteral("LENGTH"), QStringLiteral("ft"),
        candidatePoints, 1);
    candidatePlots.show();
    application.processEvents();
    QImage candidatePlotImage(candidatePlots.size(), QImage::Format_ARGB32_Premultiplied);
    candidatePlotImage.fill(Qt::transparent);
    QPainter candidatePlotPainter(&candidatePlotImage);
    candidatePlots.render(&candidatePlotPainter);
    candidatePlotPainter.end();
    auto* candidateScorePlot = candidatePlots.findChild<QWidget*>(
        QStringLiteral("optimizationCandidateScorePlot"));
    const auto candidatePlotValid = !candidatePlotImage.isNull()
        && candidateScorePlot != nullptr
        && candidateScorePlot->property("pointCount").toInt() == 3
        && candidateScorePlot->property("xValuesAscending").toBool()
        && candidateScorePlot->property("bestX").toDouble() == 10.0
        && candidateScorePlot->property("leftAxisMinimum").toDouble() == 0.0
        && candidateScorePlot->property("leftAxisMaximum").toDouble() >= 2.0;
    necwb::ui::ResultsSummaryView resultsSummary;
    resultsSummary.resize(700, 420);
    resultsSummary.setResults(result, QStringLiteral("test-run"));
    resultsSummary.setSelectedFrequency(14.1);
    auto* historicalInputPanel = resultsSummary.findChild<QWidget*>(
        QStringLiteral("historicalInputSnapshotPanel"));
    const auto historicalInputInitiallyHidden = historicalInputPanel != nullptr
        && historicalInputPanel->isHidden();
    resultsSummary.setHistoricalInputSnapshot(QStringLiteral("CM authored\nEN\n"),
        QStringLiteral("CM generated\nEN\n"));
    auto* historicalInputSource = resultsSummary.findChild<QComboBox*>(
        QStringLiteral("historicalInputSnapshotSource"));
    auto* historicalInputText = resultsSummary.findChild<QPlainTextEdit*>(
        QStringLiteral("historicalInputSnapshotText"));
    const auto authoredInputShown = historicalInputPanel != nullptr
        && !historicalInputPanel->isHidden() && historicalInputSource != nullptr
        && historicalInputSource->count() == 2 && historicalInputText != nullptr
        && historicalInputText->isReadOnly()
        && historicalInputText->toPlainText().contains(QStringLiteral("authored"));
    if (historicalInputSource != nullptr) historicalInputSource->setCurrentIndex(1);
    const auto generatedInputShown = historicalInputText != nullptr
        && historicalInputText->toPlainText().contains(QStringLiteral("generated"));
    resultsSummary.show();
    application.processEvents();
    QImage summaryImage(resultsSummary.size(), QImage::Format_ARGB32_Premultiplied);
    summaryImage.fill(Qt::transparent);
    QPainter summaryPainter(&summaryImage);
    resultsSummary.render(&summaryPainter);
    summaryPainter.end();
    necwb::ui::AverageGainResultsView averageGainResults;
    QAction runAverageGain(QStringLiteral("Run Average Gain Test"), &averageGainResults);
    auto averageGainTriggered = false;
    QObject::connect(&runAverageGain, &QAction::triggered,
        [&averageGainTriggered] { averageGainTriggered = true; });
    averageGainResults.setRunAction(&runAverageGain);
    averageGainResults.resize(700, 420);
    averageGainResults.setResult(necwb::analysis::assessAverageGain(0.98, 1.0), 14.1,
        necwb::analysis::AverageGainEnvironment::FreeSpace, 4.0,
        QStringLiteral("Model: test.nec · Run: AGT"));
    averageGainResults.show();
    application.processEvents();
    auto* averageGainTable = averageGainResults.findChild<QTableWidget*>();
    auto* runAverageGainButton = averageGainResults.findChild<QToolButton*>(
        QStringLiteral("runAverageGainFromValidationButton"));
    if (runAverageGainButton != nullptr) runAverageGainButton->click();
    if (averageGainTable == nullptr || averageGainTable->item(1, 1) == nullptr
        || averageGainTable->item(1, 1)->text() != QStringLiteral("0.980")
        || runAverageGainButton == nullptr || !averageGainTriggered) {
        return NECWB_SMOKE_FAILURE("average gain results");
    }
    QImage averageGainImage(averageGainResults.size(), QImage::Format_ARGB32_Premultiplied);
    averageGainImage.fill(Qt::transparent);
    QPainter averageGainPainter(&averageGainImage);
    averageGainResults.render(&averageGainPainter);
    averageGainPainter.end();
    necwb::ui::ConvergenceWorkspace convergence;
    convergence.resize(900, 620);
    convergence.show();
    application.processEvents();
    auto* runConvergence = convergence.findChild<QPushButton*>(
        QStringLiteral("runConvergenceStudyButton"));
    auto* convergenceHistoricalBanner = convergence.findChild<QWidget*>(
        QStringLiteral("convergenceHistoricalBanner"));
    auto* convergenceReturn = convergence.findChild<QPushButton*>(
        QStringLiteral("convergenceReturnToCurrentWorkButton"));
    const auto convergenceHistoryControlsValid = runConvergence != nullptr
        && runConvergence->text() == QStringLiteral("Run Convergence Study")
        && convergenceHistoricalBanner != nullptr && !convergenceHistoricalBanner->isVisible()
        && convergenceReturn != nullptr;
    if (!convergenceHistoryControlsValid)
        return NECWB_SMOKE_FAILURE("convergence history controls");
    QImage convergenceImage(convergence.size(), QImage::Format_ARGB32_Premultiplied);
    convergenceImage.fill(Qt::transparent);
    QPainter convergencePainter(&convergenceImage);
    convergence.render(&convergencePainter);
    convergencePainter.end();
    necwb::ui::OptimizationWorkspace optimization;
    optimization.resize(1000, 700);
    optimization.show();
    application.processEvents();
    const auto optimizationSource = QStringLiteral("SY LONG_FT=95\n"
                       "SY FT=0.3048\n"
                       "SY HALF=LONG_FT*FT/2\n"
                       "GW 1 21 -HALF 0 10 HALF 0 10 0.001\n"
                       "GE 0\nEX 0 1 11 0 1 0\nFR 0 1 0 0 7.15 0\nEN\n");
    optimization.setContext(optimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    application.processEvents();
    auto* optimizationConfiguration = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationConfigurationPanel"));
    auto* optimizationWorkspaceSplitter = optimization.findChild<QSplitter*>(
        QStringLiteral("optimizationWorkspaceSplitter"));
    auto* optimizationResultsPanel = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationResultsPanel"));
    auto* optimizationConfigurationScrollArea = optimization.findChild<QScrollArea*>(
        QStringLiteral("optimizationConfigurationScrollArea"));
    auto* optimizationSetupContent = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationSetupContent"));
    auto* optimizationVariableSection = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationVariableSection"));
    auto* optimizationFrequencySection = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationFrequencySection"));
    auto* optimizationObjectiveSection = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationObjectiveSection"));
    auto* optimizationResultsTable = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationResultsTable"));
    auto* optimizationResultsSplitter = optimization.findChild<QSplitter*>(
        QStringLiteral("optimizationResultsSplitter"));
    auto* optimizationCandidatePlots = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationCandidatePlots"));
    auto* optimizationCandidateDetailsWindow = optimization.findChild<QDialog*>(
        QStringLiteral("optimizationCandidateDetailsWindow"));
    auto* optimizationCandidateDetails = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationCandidateDetails"));
    auto* optimizationCandidateDetailViews = optimization.findChild<QTabWidget*>(
        QStringLiteral("optimizationCandidateDetailViews"));
    auto* optimizationCandidateDetailPlots = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationCandidateDetailPlots"));
    auto* optimizationCandidateDirectionalPlots = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationCandidateDirectionalPlots"));
    auto* optimizationApplyCandidate = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationApplyCandidate"));
    auto* optimizationApplyCandidateAndRun = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationApplyCandidateAndRun"));
    auto* optimizationVariables = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationVariablesTable"));
    auto* optimizationParameterSettings = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationParameterSettings"));
    auto* optimizationObjectiveSettings = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationObjectiveSettings"));
    auto* optimizationObjectiveCriteria = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationObjectiveCriteria"));
    auto* optimizationSwrWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationSwrWeight"));
    auto* optimizationResistanceWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationResistanceWeight"));
    auto* optimizationResistanceTarget = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationResistanceTarget"));
    auto* optimizationReactanceWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationReactanceWeight"));
    auto* optimizationReactanceTarget = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationReactanceTarget"));
    auto* optimizationVariableControl = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationVariableControl"));
    auto* optimizationMinimum = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationMinimum"));
    auto* optimizationMaximum = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationMaximum"));
    auto* optimizationSearchBudgetLabel = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationSearchBudgetLabel"));
    auto* optimizationStudySummary = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationStudySummary"));
    auto* optimizationFrequencyMode = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationFrequencyMode"));
    auto* optimizationFrequencySummary = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationFrequencySummary"));
    auto* optimizationEditFrequencies = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationEditFrequencies"));
    auto* optimizationFrequencyDialog = optimization.findChild<QDialog*>(
        QStringLiteral("optimizationFrequencyDialog"));
    auto* optimizationObjective = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationObjectiveControl"));
    auto* optimizationSwrGoal = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationGoal0"));
    auto* optimizationGainGoal = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationGoal3"));
    auto* optimizationSwrAggregation = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationAggregation0"));
    auto* optimizationGainAggregation = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationAggregation3"));
    auto* optimizationSwrValue = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationObjectiveValue0"));
    auto* optimizationResetObjectiveDefaults = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationResetObjectiveDefaults"));
    auto* optimizationObjectiveSummary = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationObjectiveSummary"));
    auto* optimizationObjectiveExplanation = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationObjectiveExplanation"));
    auto* optimizationEditObjective = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationEditObjective"));
    auto* optimizationObjectiveDialog = optimization.findChild<QDialog*>(
        QStringLiteral("optimizationObjectiveDialog"));
    auto* optimizationTargetFrequency = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationTargetFrequency"));
    auto* optimizationTargetFrequencyLabel = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationTargetFrequencyLabel"));
    auto* optimizationDirectionalFrequencyNote = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationDirectionalFrequencyNote"));
    auto* optimizationFrontToBackWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationFrontToBackWeight"));
    auto* optimizationFrontToRearWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationFrontToRearWeight"));
    auto* optimizationFrequencyTable = optimization.findChild<QListWidget*>(
        QStringLiteral("optimizationFrequencyTable"));
    auto* optimizationAddAmateurBand = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationAddAmateurBand"));
    auto* optimizationClearFrequencies = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationClearFrequencies"));
    auto* optimizationExplicitFrequencies = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationExplicitFrequencyPanel"));
    auto* optimizationContinuousFrequencies = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationContinuousFrequencyPanel"));
    auto* optimizationContinuousStart = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationContinuousStart"));
    auto* optimizationContinuousStop = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationContinuousStop"));
    auto* optimizationContinuousStep = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationContinuousStep"));
    auto* optimizationWorkload = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationWorkload"));
    auto* optimizationApplyBest = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationApplyBest"));
    auto* optimizationSearchMethods = optimization.findChild<QTabBar*>(
        QStringLiteral("optimizationSearchMethodTabs"));
    auto* optimizationParameterToleranceLabel = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationParameterToleranceLabel"));
    auto* optimizationScoreToleranceLabel = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationScoreToleranceLabel"));
    auto* optimizationResetSearchDefaults = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationResetSearchDefaults"));
    auto* optimizationAdaptiveMaximum = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationAdaptiveMaximumEvaluations"));
    auto* optimizationCandidateCount = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationCandidateCount"));
    auto* optimizationAdaptiveParameterTolerance = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationAdaptiveParameterTolerance"));
    auto* optimizationAdaptiveScoreTolerance = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationAdaptiveScoreTolerance"));
    auto* optimizationDifferentialEvolutionSettings = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationDifferentialEvolutionSettings"));
    auto* optimizationDifferentialEvolutionPopulation = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionPopulation"));
    auto* optimizationDifferentialEvolutionGenerations = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionGenerations"));
    auto* optimizationDifferentialEvolutionMutation = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionMutation"));
    auto* optimizationDifferentialEvolutionCrossover = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionCrossover"));
    auto* optimizationDifferentialEvolutionScoreTolerance =
        optimization.findChild<QDoubleSpinBox*>(
            QStringLiteral("optimizationDifferentialEvolutionScoreTolerance"));
    auto* optimizationDifferentialEvolutionSeed = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionSeed"));
    if (optimizationResultsTable != nullptr) optimizationResultsTable->setRowCount(1);
    optimization.setContext(optimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    const auto optimizerResultsPreserved = optimizationResultsTable != nullptr
        && optimizationResultsTable->rowCount() == 1;
    if (optimizationResultsTable != nullptr) optimizationResultsTable->setRowCount(0);
    if (optimizationFrequencyMode != nullptr) optimizationFrequencyMode->setCurrentIndex(1);
    QTimer::singleShot(0, &optimization, [&optimization] {
        auto* dialog = optimization.findChild<QDialog*>(
            QStringLiteral("optimizationAmateurBandDialog"));
        if (dialog == nullptr) return;
        auto* fortyMeters = dialog->findChild<QCheckBox*>(
            QStringLiteral("optimizationAmateurBandCheck2"));
        auto* addSelected = dialog->findChild<QPushButton*>(
            QStringLiteral("optimizationAddSelectedBands"));
        if (fortyMeters != nullptr) fortyMeters->setChecked(true);
        if (addSelected != nullptr) addSelected->click();
    });
    if (optimizationAddAmateurBand != nullptr) optimizationAddAmateurBand->click();
    QStringList selectedFrequencies;
    if (optimizationFrequencyTable != nullptr) {
        for (auto row = 0; row < optimizationFrequencyTable->count(); ++row)
            selectedFrequencies.push_back(optimizationFrequencyTable->item(row)->text());
    }
    const auto refreshedOptimizationSource = QStringLiteral("CM refreshed model\n")
        + optimizationSource;
    optimization.setContext(refreshedOptimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    auto sameModelFrequenciesPreserved = optimizationFrequencyMode != nullptr
        && optimizationFrequencyMode->currentData().toInt() == 1
        && optimizationFrequencyTable != nullptr
        && optimizationFrequencyTable->count() == selectedFrequencies.size();
    for (auto row = 0; sameModelFrequenciesPreserved
         && row < optimizationFrequencyTable->count(); ++row) {
        sameModelFrequenciesPreserved = optimizationFrequencyTable->item(row)->text()
            == selectedFrequencies.at(row);
    }
    necwb::ui::AnalysisRunStore optimizationRunStore;
    auto historicalOptimization = optimizationRunStore.create(QStringLiteral("nec2"),
        QStringLiteral("historical.nec"), QStringLiteral("optimization-session"));
    historicalOptimization.status = QStringLiteral("Completed");
    optimizationRunStore.save(historicalOptimization);
    auto historicalCandidate = optimizationRunStore.create(QStringLiteral("nec2"),
        QStringLiteral("historical.nec"), QStringLiteral("optimization-candidate"),
        historicalOptimization.id);
    historicalCandidate.status = QStringLiteral("Completed");
    optimizationRunStore.save(historicalCandidate);
    QFile historicalMetadata(QDir(historicalOptimization.directory).filePath(
        QStringLiteral("optimization-session.json")));
    const auto historicalMetadataWritten = historicalMetadata.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && historicalMetadata.write(
            "{\"frequencyMode\":\"explicit\",\"frequenciesMHz\":[7,14],"
            "\"variable\":\"LONG_FT\",\"variables\":[{\"name\":\"LONG_FT\","
            "\"resolvedValue\":95,\"minimum\":80,\"maximum\":110,"
            "\"tolerance\":0.01},{\"name\":\"FT\",\"resolvedValue\":0.3048,"
            "\"minimum\":0.25,\"maximum\":0.35,\"tolerance\":0.001}]}") > 0;
    historicalMetadata.close();
    QFile candidateMetadata(QDir(historicalCandidate.directory).filePath(
        QStringLiteral("optimization.json")));
    const auto candidateMetadataWritten = candidateMetadata.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && candidateMetadata.write(
            "{\"variable\":\"LONG_FT\",\"value\":90,\"variables\":[{"
            "\"name\":\"LONG_FT\",\"value\":90},{\"name\":\"FT\",\"value\":0.3}],"
            "\"objective\":\"worst-point-across-frequencies\","
            "\"referenceImpedance\":50,\"swrWeight\":1,\"resistanceWeight\":1,"
            "\"resistanceTargetOhms\":50}") > 0;
    candidateMetadata.close();
    const QByteArray candidateOutput =
        " FREQUENCY : 7.0000E+00 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n"
        " FREQUENCY : 1.4000E+01 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 7.5E+01 1.5E+01 0 0 5.0E-03\n";
    QFile candidateOutputFile(QDir(historicalCandidate.directory).filePath(
        QStringLiteral("model.out")));
    const auto candidateOutputWritten = candidateOutputFile.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && candidateOutputFile.write(candidateOutput) == candidateOutput.size();
    candidateOutputFile.close();
    const auto historicalFrequencySelectionLoaded = historicalMetadataWritten
        && candidateMetadataWritten && candidateOutputWritten
        && optimization.loadSession(historicalOptimization.id)
        && optimizationFrequencyTable != nullptr
        && optimizationFrequencyTable->count() == 2;
    auto* workspaceCandidateScorePlot = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationCandidateScorePlot"));
    const auto historicalObjectiveBreakdownLoaded = historicalFrequencySelectionLoaded
        && workspaceCandidateScorePlot != nullptr
        && workspaceCandidateScorePlot->property("pointCount").toInt() == 1
        && workspaceCandidateScorePlot->property("seriesCount").toInt() == 3
        && workspaceCandidateScorePlot->property("xAxisLabel").toString()
            == QStringLiteral("Evaluation Number");
    optimization.leaveHistoricalSession();
    auto activeFrequenciesRestored = optimizationFrequencyMode != nullptr
        && optimizationFrequencyMode->currentData().toInt() == 1
        && optimizationFrequencyTable != nullptr
        && optimizationFrequencyTable->count() == selectedFrequencies.size();
    for (auto row = 0; activeFrequenciesRestored
         && row < optimizationFrequencyTable->count(); ++row) {
        activeFrequenciesRestored = optimizationFrequencyTable->item(row)->text()
            == selectedFrequencies.at(row);
    }
    auto appliedCandidateValues = false;
    optimization.setApplyParameterCallback(
        [&optimization, &refreshedOptimizationSource, &appliedCandidateValues](
            const std::vector<std::pair<QString, double>>& values) {
            appliedCandidateValues = values.size() == 2
                && values.front().first == QStringLiteral("LONG_FT")
                && values.front().second == 90.0
                && values[1].first == QStringLiteral("FT")
                && values[1].second == 0.3;
            auto appliedSource = refreshedOptimizationSource;
            appliedSource.replace(QStringLiteral("LONG_FT=95"), QStringLiteral("LONG_FT=90"));
            appliedSource.replace(QStringLiteral("FT=0.3048"), QStringLiteral("FT=0.3"));
            optimization.setContext(appliedSource, QStringLiteral("symbol-units.nec"),
                QStringLiteral("nec2"), {}, 120, false);
            return true;
        });
    optimization.setApplyAndRunCallback([](const auto&) { return true; });
    optimization.setContext(refreshedOptimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, true);
    optimizationResultsTable->cellDoubleClicked(0, 0);
    application.processEvents();
    auto* candidateImpedancePlot = optimizationCandidateDetailPlots->findChild<QWidget*>(
        QStringLiteral("impedanceSweepPlot"));
    const auto candidatePlotHasResults = [&optimizationCandidateDetailPlots] {
        const auto labels = optimizationCandidateDetailPlots->findChildren<QLabel*>();
        return std::ranges::any_of(labels, [](const auto* label) {
            return label->text().contains(QStringLiteral("2 result point"));
        });
    };
    const auto candidateDetailsLoaded = optimizationCandidateDetails->rowCount() == 2
        && candidateImpedancePlot != nullptr
        && candidatePlotHasResults()
        && !optimizationCandidateDetailViews->isTabEnabled(2)
        && optimizationCandidateDetailViews->tabToolTip(2).contains(
            QStringLiteral("did not evaluate"))
        && optimizationApplyCandidate->isEnabled()
        && optimizationApplyCandidateAndRun->isEnabled();
    optimizationApplyCandidate->click();
    application.processEvents();
    const auto candidateApplyPreservesDetails = candidateDetailsLoaded
        && appliedCandidateValues && optimizationResultsTable->rowCount() == 1
        && optimizationCandidateDetails->rowCount() == 2
        && candidatePlotHasResults();
    optimizationCandidateDetailsWindow->hide();
    optimization.restoreCandidateDetailsWindow();
    application.processEvents();
    const auto hiddenCandidateDetailsRestored = optimizationCandidateDetailsWindow->isVisible();
    optimizationCandidateDetailsWindow->showMinimized();
    application.processEvents();
    optimization.restoreCandidateDetailsWindow();
    application.processEvents();
    const auto minimizedCandidateDetailsRestored = optimizationCandidateDetailsWindow->isVisible()
        && !optimizationCandidateDetailsWindow->isMinimized();
    optimizationCandidateDetailsWindow->hide();
    optimization.setContext(refreshedOptimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    optimizationRunStore.removeGroup(
        historicalOptimization.id, historicalOptimization.directory);
    QTimer::singleShot(0, &optimization, [&optimization] {
        auto* dialog = optimization.findChild<QDialog*>(
            QStringLiteral("optimizationFrequencyDialog"));
        auto* mode = optimization.findChild<QComboBox*>(
            QStringLiteral("optimizationFrequencyMode"));
        if (mode != nullptr) mode->setCurrentIndex(2);
        if (dialog != nullptr) dialog->reject();
    });
    if (optimizationEditFrequencies != nullptr) optimizationEditFrequencies->click();
    const auto frequencyCancelRestored = optimizationFrequencyMode != nullptr
        && optimizationFrequencyMode->currentData().toInt() == 1
        && optimizationFrequencyTable != nullptr
        && optimizationFrequencyTable->count() == selectedFrequencies.size();
    optimization.resize(760, 560);
    application.processEvents();
    const auto frequencyControlsStructured = optimizationExplicitFrequencies != nullptr
        && optimizationAddAmateurBand != nullptr
        && optimizationExplicitFrequencies->isAncestorOf(optimizationAddAmateurBand);
    if (optimizationFrequencyDialog != nullptr) optimizationFrequencyDialog->show();
    application.processEvents();
    const auto frequencyShortcuts = optimizationFrequencyTable == nullptr
        ? QList<QShortcut*>{} : optimizationFrequencyTable->findChildren<QShortcut*>();
    const auto hasDeleteShortcut = std::ranges::any_of(frequencyShortcuts, [](const auto* shortcut) {
        return shortcut->key() == QKeySequence(Qt::Key_Delete);
    });
    const auto hasBackspaceShortcut = std::ranges::any_of(
        frequencyShortcuts, [](const auto* shortcut) {
            return shortcut->key() == QKeySequence(Qt::Key_Backspace);
        });
    if (optimizationFrequencyMode != nullptr) optimizationFrequencyMode->setCurrentIndex(2);
    if (optimizationContinuousStart != nullptr) optimizationContinuousStart->setValue(14.0);
    if (optimizationContinuousStop != nullptr) optimizationContinuousStop->setValue(14.35);
    if (optimizationContinuousStep != nullptr) optimizationContinuousStep->setValue(0.01);
    application.processEvents();
    const auto continuousSweepValid = optimizationContinuousFrequencies != nullptr
        && optimizationContinuousFrequencies->isVisible()
        && optimizationExplicitFrequencies != nullptr
        && !optimizationExplicitFrequencies->isVisible()
        && optimizationWorkload != nullptr
        && optimizationWorkload->text().contains(
            QStringLiteral("7 candidates × 36 frequencies"));
    if (optimizationFrequencyDialog != nullptr) optimizationFrequencyDialog->hide();
    QTimer::singleShot(0, &optimization, [&optimization] {
        auto* dialog = optimization.findChild<QDialog*>(
            QStringLiteral("optimizationObjectiveDialog"));
        auto* goal = optimization.findChild<QComboBox*>(
            QStringLiteral("optimizationGoal0"));
        auto* reference = optimization.findChild<QDoubleSpinBox*>(
            QStringLiteral("optimizationReferenceImpedance"));
        if (goal != nullptr) goal->setCurrentIndex(1);
        if (reference != nullptr) reference->setValue(75.0);
        if (dialog != nullptr) dialog->reject();
    });
    if (optimizationEditObjective != nullptr) optimizationEditObjective->click();
    const auto objectiveCancelRestored = optimizationSwrGoal != nullptr
        && optimizationSwrGoal->currentIndex() == 0
        && optimizationObjectiveSummary != nullptr
        && optimizationObjectiveSummary->text().contains(QStringLiteral("50.000"));
    auto goalAwareBandEvaluationValid = false;
    if (optimizationSwrGoal != nullptr && optimizationSwrAggregation != nullptr) {
        optimizationSwrGoal->setCurrentIndex(1);
        application.processEvents();
        const auto maximizeWorst = optimizationSwrAggregation->currentText()
                == QStringLiteral("Worst Point")
            && optimizationSwrAggregation->currentData().toInt()
                == static_cast<int>(necwb::analysis::OptimizationAggregation::Minimum);
        optimizationSwrGoal->setCurrentIndex(2);
        application.processEvents();
        const auto targetWorst = optimizationSwrAggregation->currentText()
                == QStringLiteral("Worst Error")
            && optimizationSwrAggregation->currentData().toInt()
                == static_cast<int>(necwb::analysis::OptimizationAggregation::Maximum)
            && optimizationSwrAggregation->toolTip().contains(
                QStringLiteral("largest absolute error"));
        if (optimizationResetObjectiveDefaults != nullptr)
            optimizationResetObjectiveDefaults->click();
        application.processEvents();
        goalAwareBandEvaluationValid = maximizeWorst && targetWorst
            && !optimizationSwrGoal->toolTip().isEmpty()
            && !optimizationSwrAggregation->toolTip().isEmpty()
            && optimizationSwrGoal->currentText() == QStringLiteral("Minimize")
            && optimizationSwrAggregation->currentText() == QStringLiteral("Worst Point")
            && optimizationSwrAggregation->currentData().toInt()
                == static_cast<int>(necwb::analysis::OptimizationAggregation::Maximum)
            && optimizationObjectiveExplanation != nullptr
            && optimizationObjectiveExplanation->text().contains(
                QStringLiteral("Minimize the highest SWR"));
    }
    if (optimizationFrontToBackWeight != nullptr)
        optimizationFrontToBackWeight->setValue(1.0);
    application.processEvents();
    const auto perCriterionDirectionalObjectiveValid = optimizationObjective != nullptr
        && !optimizationObjective->isVisible()
        && optimizationTargetFrequency != nullptr
        && !optimizationTargetFrequency->isVisible()
        && optimizationTargetFrequencyLabel != nullptr
        && !optimizationTargetFrequencyLabel->isVisible()
        && optimizationDirectionalFrequencyNote != nullptr
        && optimizationDirectionalFrequencyNote->text().contains(
            QStringLiteral("Each criterion independently"));
    if (optimizationFrontToBackWeight != nullptr)
        optimizationFrontToBackWeight->setValue(0.0);
    const auto perCriterionFrequencyPlanValid = optimizationFrequencyMode != nullptr
        && optimizationFrequencyMode->isEnabled()
        && optimizationContinuousFrequencies != nullptr
        && !optimizationContinuousFrequencies->isHidden();
    if (optimizationSearchMethods != nullptr) optimizationSearchMethods->setCurrentIndex(1);
    application.processEvents();
    if (optimizationVariables != nullptr && optimizationVariables->rowCount() > 1)
        optimizationVariables->item(1, 0)->setCheckState(Qt::Checked);
    if (optimizationAdaptiveMaximum != nullptr) optimizationAdaptiveMaximum->setValue(35);
    if (optimizationAdaptiveParameterTolerance != nullptr)
        optimizationAdaptiveParameterTolerance->setValue(2.0);
    if (optimizationAdaptiveScoreTolerance != nullptr)
        optimizationAdaptiveScoreTolerance->setValue(3.0);
    if (optimizationResetSearchDefaults != nullptr) optimizationResetSearchDefaults->click();
    const auto adaptiveGridAligned = optimizationVariables != nullptr
        && optimizationVariables->isVisible()
        && optimizationVariableControl != nullptr && !optimizationVariableControl->isVisible()
        && optimizationMinimum != nullptr && !optimizationMinimum->isVisible()
        && optimizationMaximum != nullptr && !optimizationMaximum->isVisible()
        && optimizationAdaptiveMaximum != nullptr && optimizationAdaptiveMaximum->isVisible()
        && optimizationAdaptiveScoreTolerance != nullptr
        && optimizationAdaptiveScoreTolerance->isVisible()
        && optimizationSearchBudgetLabel != nullptr
        && optimizationSearchBudgetLabel->alignment()
            == (Qt::AlignRight | Qt::AlignVCenter);
    const auto adaptiveControlsValid = optimizationSearchMethods != nullptr
        && optimizationSearchMethods->count() == 4
        && optimizationSearchMethods->tabText(0) == QStringLiteral("Parameter Sweep")
        && optimizationSearchMethods->tabText(1) == QStringLiteral("Adaptive Optimize")
        && optimizationSearchMethods->tabText(2) == QStringLiteral("Nelder-Mead")
        && optimizationSearchMethods->tabText(3) == QStringLiteral("Differential Evolution")
        && optimizationParameterToleranceLabel != nullptr
        && !optimizationParameterToleranceLabel->isVisible()
        && optimizationScoreToleranceLabel != nullptr
        && optimizationScoreToleranceLabel->isVisible()
        && optimizationAdaptiveMaximum != nullptr && optimizationAdaptiveMaximum->value() == 21
        && optimizationAdaptiveParameterTolerance != nullptr
        && optimizationAdaptiveParameterTolerance->value() == 0.010
        && optimizationAdaptiveScoreTolerance != nullptr
        && optimizationAdaptiveScoreTolerance->value() == 0.001
        && optimizationWorkload != nullptr
        && optimizationWorkload->text().contains(QStringLiteral("Up to 21 candidates"))
        && optimizationStudySummary != nullptr
        && optimizationStudySummary->text().contains(QStringLiteral("2 parameter(s)"))
        && adaptiveGridAligned;
    if (optimizationSearchMethods != nullptr) optimizationSearchMethods->setCurrentIndex(2);
    application.processEvents();
    const auto nelderMeadControlsValid = optimizationSearchMethods != nullptr
        && !optimizationSearchMethods->tabToolTip(2).isEmpty()
        && optimizationVariables != nullptr && optimizationVariables->isVisible()
        && optimizationVariableControl != nullptr && !optimizationVariableControl->isVisible()
        && optimizationAdaptiveMaximum != nullptr && optimizationAdaptiveMaximum->isVisible()
        && optimizationAdaptiveScoreTolerance != nullptr
        && optimizationAdaptiveScoreTolerance->isVisible()
        && optimizationWorkload != nullptr
        && optimizationWorkload->text().contains(QStringLiteral("Up to 21 candidates"));
    if (optimizationSearchMethods != nullptr) optimizationSearchMethods->setCurrentIndex(3);
    application.processEvents();
    const auto differentialEvolutionControlsValid = optimizationSearchMethods != nullptr
        && !optimizationSearchMethods->tabToolTip(3).isEmpty()
        && optimizationVariables != nullptr && optimizationVariables->isVisible()
        && optimizationVariableControl != nullptr && !optimizationVariableControl->isVisible()
        && optimizationDifferentialEvolutionSettings != nullptr
        && optimizationDifferentialEvolutionSettings->isVisible()
        && optimizationAdaptiveMaximum != nullptr && !optimizationAdaptiveMaximum->isVisible()
        && optimizationAdaptiveScoreTolerance != nullptr
        && !optimizationAdaptiveScoreTolerance->isVisible()
        && optimizationDifferentialEvolutionPopulation != nullptr
        && optimizationDifferentialEvolutionPopulation->value() == 12
        && optimizationDifferentialEvolutionGenerations != nullptr
        && optimizationDifferentialEvolutionGenerations->value() == 20
        && optimizationDifferentialEvolutionMutation != nullptr
        && optimizationDifferentialEvolutionMutation->value() == 0.8
        && optimizationDifferentialEvolutionCrossover != nullptr
        && optimizationDifferentialEvolutionCrossover->value() == 0.9
        && optimizationDifferentialEvolutionScoreTolerance != nullptr
        && optimizationDifferentialEvolutionScoreTolerance->value() == 0.001
        && optimizationDifferentialEvolutionSeed != nullptr
        && optimizationDifferentialEvolutionSeed->value() == 5489
        && optimizationWorkload != nullptr
        && optimizationWorkload->text().contains(QStringLiteral("Up to 252 candidates"));
    if (optimizationSearchMethods != nullptr) optimizationSearchMethods->setCurrentIndex(0);
    if (optimizationCandidateCount != nullptr) optimizationCandidateCount->setValue(12);
    if (optimizationResetSearchDefaults != nullptr) optimizationResetSearchDefaults->click();
    application.processEvents();
    const auto sweepControlsValid = optimizationParameterToleranceLabel != nullptr
        && !optimizationParameterToleranceLabel->isVisible()
        && optimizationScoreToleranceLabel != nullptr
        && !optimizationScoreToleranceLabel->isVisible()
        && optimizationCandidateCount != nullptr
        && optimizationCandidateCount->value() == 7;
    auto wheelProtected = false;
    if (optimizationCandidateCount != nullptr) {
        optimizationCandidateCount->clearFocus();
        const auto priorValue = optimizationCandidateCount->value();
        QWheelEvent wheelEvent(QPointF(4, 4), QPointF(4, 4), {}, QPoint(0, 120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(optimizationCandidateCount, &wheelEvent);
        wheelProtected = optimizationCandidateCount->value() == priorValue;
    }
    const auto optimizationDecimalControls = optimization.findChildren<QDoubleSpinBox*>();
    const auto optimizerUsesThreeDecimals = std::ranges::all_of(
        optimizationDecimalControls, [](const auto* control) { return control->decimals() == 3; });
    const auto compactSetupValid = optimizationSetupContent != nullptr
        && optimizationVariableSection != nullptr
        && optimizationFrequencySection != nullptr
        && optimizationObjectiveSection != nullptr
        && optimization.findChild<QTabWidget*>(
            QStringLiteral("optimizationConfigurationTabs")) == nullptr
        && optimization.findChild<QTabWidget*>(
            QStringLiteral("optimizationResultViews")) == nullptr;
    if (optimizationConfiguration == nullptr
        || optimizationWorkspaceSplitter == nullptr
        || optimizationWorkspaceSplitter->orientation() != Qt::Horizontal
        || optimizationWorkspaceSplitter->count() != 2
        || optimizationWorkspaceSplitter->childrenCollapsible()
        || optimizationResultsPanel == nullptr
        || optimizationConfigurationScrollArea == nullptr
        || optimizationConfigurationScrollArea->verticalScrollBarPolicy()
            != Qt::ScrollBarAsNeeded
        || optimizationConfigurationScrollArea->horizontalScrollBarPolicy()
            != Qt::ScrollBarAlwaysOff
        || optimizationResultsTable == nullptr
        || optimizationCandidatePlots == nullptr
        || optimizationCandidateDetailsWindow == nullptr
        || optimizationCandidateDetailsWindow->isVisible()
        || optimizationCandidateDetails == nullptr
        || optimizationCandidateDetails->columnCount() != 7
        || !optimizationCandidateDetailsWindow->isAncestorOf(
            optimizationCandidateDetails)
        || optimizationCandidateDetailViews == nullptr
        || optimizationCandidateDetailViews->count() != 3
        || optimizationCandidateDetailViews->tabText(0) != QStringLiteral("Frequency Table")
        || optimizationCandidateDetailViews->tabText(1)
            != QStringLiteral("SWR & Impedance Plots")
        || optimizationCandidateDetailViews->tabText(2)
            != QStringLiteral("Directional Plots")
        || optimizationCandidateDetailViews->isTabEnabled(2)
        || !optimizationCandidateDetailViews->tabToolTip(2).contains(
            QStringLiteral("Gain, F/B, or F/R"))
        || optimizationCandidateDetailPlots == nullptr
        || !optimizationCandidateDetailsWindow->isAncestorOf(
            optimizationCandidateDetailPlots)
        || optimizationCandidateDirectionalPlots == nullptr
        || !optimizationCandidateDetailsWindow->isAncestorOf(
            optimizationCandidateDirectionalPlots)
        || optimizationApplyCandidate == nullptr || optimizationApplyCandidate->isEnabled()
        || optimizationApplyCandidateAndRun == nullptr
        || optimizationApplyCandidateAndRun->isEnabled()
        || optimizationApplyCandidateAndRun->toolTip().isEmpty()
        || optimizationResultsSplitter == nullptr
        || optimizationResultsSplitter->orientation() != Qt::Vertical
        || optimizationResultsSplitter->count() != 2
        || optimizationResultsSplitter->childrenCollapsible()
        || !optimizationResultsSplitter->isAncestorOf(optimizationResultsTable)
        || !optimizationResultsSplitter->isAncestorOf(optimizationCandidatePlots)
        || optimizationVariables == nullptr
        || optimizationVariableControl == nullptr
        || optimizationMinimum == nullptr
        || optimizationMaximum == nullptr
        || optimizationSearchBudgetLabel == nullptr
        || !differentialEvolutionControlsValid
        || optimizationParameterSettings == nullptr
        || optimizationObjectiveSettings == nullptr
        || optimizationObjectiveCriteria == nullptr
        || optimizationSwrWeight == nullptr || optimizationSwrWeight->value() != 1.0
        || optimizationResistanceWeight == nullptr || optimizationResistanceWeight->value() != 0.0
        || optimizationResistanceTarget == nullptr || optimizationResistanceTarget->value() != 50.0
        || optimizationReactanceWeight == nullptr || optimizationReactanceWeight->value() != 0.0
        || optimizationReactanceTarget == nullptr || optimizationReactanceTarget->value() != 0.0
        || optimizationStudySummary == nullptr
        || !optimizationStudySummary->text().contains(QStringLiteral("LONG_FT"))
        || !optimizationStudySummary->text().contains(QStringLiteral("Frequencies:"))
        || optimizationFrequencyMode == nullptr
        || optimizationFrequencySummary == nullptr
        || optimizationFrequencySummary->text().isEmpty()
        || optimizationEditFrequencies == nullptr
        || optimizationFrequencyDialog == nullptr
        || optimizationObjective == nullptr
        || optimizationObjective->count() != 1
        || optimizationObjectiveSummary == nullptr
        || optimizationObjectiveSummary->text().isEmpty()
        || optimizationEditObjective == nullptr
        || optimizationObjectiveDialog == nullptr
        || optimizationSwrGoal == nullptr || optimizationSwrGoal->count() != 5
        || optimizationSwrGoal->currentText() != QStringLiteral("Minimize")
        || optimizationGainGoal == nullptr
        || optimizationGainGoal->currentText() != QStringLiteral("Maximize")
        || optimizationSwrAggregation == nullptr
        || optimizationSwrAggregation->currentText() != QStringLiteral("Worst Point")
        || optimizationGainAggregation == nullptr
        || optimizationGainAggregation->currentText() != QStringLiteral("Worst Point")
        || optimizationSwrValue == nullptr || optimizationSwrValue->value() != 2.0
        || optimizationResetObjectiveDefaults == nullptr
        || optimizationObjectiveExplanation == nullptr
        || !goalAwareBandEvaluationValid
        || optimizationTargetFrequency == nullptr
        || optimizationFrontToRearWeight == nullptr
        || optimizationFrequencyTable == nullptr
        || optimizationAddAmateurBand == nullptr
        || optimizationClearFrequencies == nullptr
        || optimizationExplicitFrequencies == nullptr
        || optimizationContinuousStart == nullptr
        || optimizationContinuousStop == nullptr
        || optimizationContinuousStep == nullptr
        || optimizationApplyBest == nullptr || optimizationApplyBest->isEnabled()
        || optimizationResetSearchDefaults == nullptr
        || optimizationCandidateCount == nullptr
        || !optimizationCandidateCount->toolTip().contains(
            QStringLiteral("including both endpoints"))
        || !optimizationCandidateCount->toolTip().contains(
            QStringLiteral("every selected frequency"))
        || !adaptiveControlsValid
        || !nelderMeadControlsValid
        || !sweepControlsValid
        || !wheelProtected
        || !compactSetupValid
        || !frequencyControlsStructured
        || !hasDeleteShortcut
        || !hasBackspaceShortcut
        || !continuousSweepValid
        || !perCriterionFrequencyPlanValid
        || optimizationFrequencyTable->viewMode() != QListView::IconMode
        || !optimizationFrequencyTable->isWrapping()
        || optimizationFrequencyTable->count() != 7
        || !optimizerUsesThreeDecimals
        || !optimizerResultsPreserved
        || !sameModelFrequenciesPreserved
        || !historicalFrequencySelectionLoaded
        || !historicalObjectiveBreakdownLoaded
        || !historicalInputInitiallyHidden || !authoredInputShown || !generatedInputShown
        || !activeFrequenciesRestored
        || !candidateApplyPreservesDetails
        || !hiddenCandidateDetailsRestored
        || !minimizedCandidateDetailsRestored
        || !frequencyCancelRestored
        || !objectiveCancelRestored
        || !perCriterionDirectionalObjectiveValid
        || optimizationObjectiveCriteria->rowCount() != 6
        || optimizationObjectiveCriteria->columnCount() != 5
        || optimizationObjectiveCriteria->horizontalHeaderItem(4)->text()
            != QStringLiteral("Band Evaluation")
        || optimizationResultsTable->columnCount() != 11
        || optimizationVariables->columnCount() != 6
        || optimizationVariables->rowCount() != 3
        || optimizationVariables->item(0, 0)->checkState() != Qt::Checked
        || optimizationVariables->item(1, 0)->checkState() != Qt::Checked
        || optimizationVariables->item(0, 1)->text() != QStringLiteral("LONG_FT")
        || optimizationVariables->item(0, 1)->toolTip().isEmpty()
        || optimizationVariables->item(0, 2)->text() != QStringLiteral("95.000")
        || optimizationVariables->item(0, 3)->text() != QStringLiteral("76.000")
        || optimizationVariables->item(0, 4)->text() != QStringLiteral("114.000")
        || optimizationVariableControl->count() != 3
        || optimizationVariableControl->findText(QStringLiteral("HALF")) < 0) {
        return NECWB_SMOKE_FAILURE("optimization workspace");
    }
    necwb::ui::CurrentDistributionView currents;
    necwb::ui::RadiationPatternView radiation2D;
    necwb::ui::Radiation3DView radiation3D;
    necwb::ui::RadiationPerformanceView radiationPerformance;
    for (auto* resultView : std::vector<QWidget*>{
             &currents, &radiation2D, &radiation3D, &radiationPerformance}) {
        resultView->resize(800, 600);
    }
    necwb::model::AntennaModel currentAntenna;
    currentAntenna.addWire({1, {0.0, 0.0, 0.0}, {5.0, 0.0, 0.0}, 2, 0.001, 1});
    currentAntenna.addWire({2, {0.0, 0.0, 0.0}, {-3.0, 0.0, 0.0}, 2, 0.001, 2});
    const std::vector<necwb::analysis::SegmentCurrentResult> unorderedCurrents{
        {.segment = 1, .wireTag = 1, .magnitude = 5.0},
        {.segment = 2, .wireTag = 1, .magnitude = 1.0},
        {.segment = 1, .wireTag = 2, .magnitude = 4.0},
        {.segment = 2, .wireTag = 2, .magnitude = 2.0}};
    const auto currentPaths = necwb::ui::connectedCurrentPaths(unorderedCurrents, currentAntenna);
    if (currentPaths.size() != 1
        || currentPaths.front().front().wireTag != 2
        || currentPaths.front().front().segment != 2
        || currentPaths.front()[1].segment != 1
        || currentPaths.front()[2].wireTag != 1
        || currentPaths.front()[2].segment != 1
        || currentPaths.front().back().segment != 2) {
        return NECWB_SMOKE_FAILURE("current distribution paths");
    }
    currents.setModel(currentAntenna);
    currents.setResults(result, QStringLiteral("test-run"));
    currents.setSelectedFrequency(14.1);
    radiation2D.setResults(result, QStringLiteral("test-run"));
    radiation2D.resize(800, 800);
    radiation2D.show();
    application.processEvents();
    auto* radiationPolarPlot = radiation2D.findChild<QWidget*>(
        QStringLiteral("radiationPolarPlot"));
    auto* radiationHoverReadout = radiation2D.findChild<QLabel*>(
        QStringLiteral("radiation2DHoverReadout"));
    if (radiationPolarPlot != nullptr) {
        const QPoint hoverPoint(radiationPolarPlot->width() / 2,
            radiationPolarPlot->height() / 4);
        QMouseEvent hoverEvent(QEvent::MouseMove, QPointF(hoverPoint),
            QPointF(radiationPolarPlot->mapToGlobal(hoverPoint)), Qt::NoButton,
            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(radiationPolarPlot, &hoverEvent);
        application.processEvents();
    }
    std::vector<QPointF> fullCircle;
    for (auto angle = 0; angle < 360; angle += 10) fullCircle.emplace_back(angle, 1.0);
    std::vector<QPointF> partialCircle;
    for (auto angle = 0; angle <= 90; angle += 10) partialCircle.emplace_back(angle, 1.0);
    if (radiationPolarPlot == nullptr
        || radiationHoverReadout == nullptr
        || !radiationHoverReadout->text().contains(QStringLiteral("Nearest NEC sample"))
        || !radiationHoverReadout->text().contains(QStringLiteral("dBi"))
        || !radiationHoverReadout->text().contains(QStringLiteral("Relative"))
        || radiationPolarPlot->property("hoverReadout").toString().isEmpty()
        || !radiationPolarPlot->property("closedPattern").toBool()
        || radiationPolarPlot->property("labelColorRole").toInt()
            != static_cast<int>(QPalette::Text)
        || !radiationPolarPlot->property("ringLabelBackground").toBool()
        || radiationPolarPlot->property("hoverSampleMode").toString()
            != QStringLiteral("nearest-nec-sample")
        || !necwb::ui::radiationAnglesCoverCircle(fullCircle)
        || necwb::ui::radiationAnglesCoverCircle(partialCircle)) {
        return NECWB_SMOKE_FAILURE("radiation cut analysis");
    }
    necwb::model::AntennaModel antenna;
    antenna.addWire({1, {0.0, 0.0, -0.5}, {0.0, 0.0, 0.5}, 11, 0.001, 1});
    radiation3D.setModel(antenna);
    necwb::model::AntennaModel elevatedAntenna;
    elevatedAntenna.addWire({1, {-5.0, 0.0, 6.1}, {5.0, 0.0, 6.1}, 11, 0.001, 1});
    const auto resultOriginPreserved = std::abs(
        necwb::ui::resultModelExtentFromOrigin(elevatedAntenna) - 6.1) < 1.0e-12;
    necwb::model::AntennaModel curvedAntenna;
    auto curvedWire = necwb::model::Wire{2, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
        2, 0.001, 2};
    curvedWire.path = {{-1.0, 0.0, 0.0}, {0.0, 0.0, 4.0}, {1.0, 0.0, 0.0}};
    curvedAntenna.addWire(std::move(curvedWire));
    const auto semanticResultExtent = std::abs(
        necwb::ui::resultModelExtentFromOrigin(curvedAntenna) - 4.0) < 1.0e-12;
    radiation3D.setResults(result, QStringLiteral("test-run"));
    radiationPerformance.setResults(result, QStringLiteral("test-run"));
    radiationPerformance.setSelectedFrequency(14.2);
    radiation2D.setSettingsChangedCallback([&](const auto& settings) {
        radiation3D.setDisplaySettings(settings);
        radiationPerformance.setComponent(settings.component);
    });
    radiation3D.setSettingsChangedCallback([&](const auto& settings) {
        radiation2D.setDisplaySettings(settings);
        radiationPerformance.setComponent(settings.component);
    });
    radiationPerformance.setComponentChangedCallback([&](auto component) {
        radiation2D.setComponent(component);
        radiation3D.setComponent(component);
    });
    auto* radiation2DComponent = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DComponent"));
    auto* radiation2DFrequency = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DFrequency"));
    auto* radiation3DComponent = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DComponent"));
    auto* radiationPerformanceComponent = radiationPerformance.findChild<QComboBox*>(
        QStringLiteral("radiationPerformanceComponent"));
    auto* radiation3DFrequency = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DFrequency"));
    auto* radiation2DFloor = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DFloor"));
    auto* radiation3DFloor = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DFloor"));
    auto* radiation2DSummary = radiation2D.findChild<QLabel*>(QStringLiteral("radiation2DSummary"));
    auto* radiation2DCutPlane = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DCutPlane"));
    auto* radiation2DDataset = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DDataset"));
    auto* radiation3DDataset = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DDataset"));
    auto* radiation2DOrientation = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DOrientation"));
    auto* radiation2DMaxGainCut = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DMaxGainCut"));
    auto* radiation3DSummary = radiation3D.findChild<QLabel*>(QStringLiteral("radiation3DSummary"));
    auto* radiation3DHoverReadout = radiation3D.findChild<QLabel*>(
        QStringLiteral("radiation3DHoverReadout"));
    auto* radiation3DSurface = radiation3D.findChild<QWidget*>(
        QStringLiteral("radiation3DSurface"));
    auto* radiation2DExportImage = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DExportImage"));
    auto* radiation2DExportData = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DExportData"));
    auto* radiation3DExportImage = radiation3D.findChild<QPushButton*>(QStringLiteral("radiation3DExportImage"));
    auto* radiation3DExportData = radiation3D.findChild<QPushButton*>(QStringLiteral("radiation3DExportData"));
    auto* radiationPerformanceSummary = radiationPerformance.findChild<QLabel*>(
        QStringLiteral("radiationPerformanceSummary"));
    auto* radiationPerformancePlot = radiationPerformance.findChild<QWidget*>(
        QStringLiteral("directionalMetricsPlot"));
    auto* radiationPerformanceTheta = radiationPerformance.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationPerformanceTheta"));
    if (radiation2DComponent == nullptr || radiation2DFrequency == nullptr
        || radiation3DComponent == nullptr || radiation3DFrequency == nullptr
        || radiationPerformanceComponent == nullptr
        || radiation2DFloor == nullptr || radiation3DFloor == nullptr
        || radiation2DSummary == nullptr || radiation3DSummary == nullptr
        || radiation3DHoverReadout == nullptr || radiation3DSurface == nullptr
        || radiation2DCutPlane == nullptr || radiation2DOrientation == nullptr
        || radiation2DDataset == nullptr || radiation3DDataset == nullptr
        || radiation2DMaxGainCut == nullptr
        || radiationPerformanceSummary == nullptr || radiationPerformancePlot == nullptr
        || radiationPerformanceTheta == nullptr
        || radiation2DExportImage == nullptr || radiation2DExportData == nullptr
        || radiation3DExportImage == nullptr || radiation3DExportData == nullptr)
        return NECWB_SMOKE_FAILURE("radiation result controls");
    radiation3D.resize(900, 700);
    radiation3D.show();
    application.processEvents();
    const auto legendColorCount = [](const QImage& image) {
        auto count = 0;
        for (auto y = 0; y < image.height(); ++y) {
            for (auto x = 0; x < image.width(); ++x) {
                if (QColor::fromRgba(image.pixel(x, y)).hsvSaturation() > 80) ++count;
            }
        }
        return count;
    };
    const auto legendRegion = QRect(10, 42, 285, 108);
    const auto legendColorsBeforeHover = legendColorCount(
        radiation3DSurface->grab(legendRegion).toImage());
    const QPoint radiation3DHoverPoint(radiation3DSurface->width() / 2,
        radiation3DSurface->height() / 3);
    QMouseEvent radiation3DHoverEvent(QEvent::MouseMove, QPointF(radiation3DHoverPoint),
        QPointF(radiation3DSurface->mapToGlobal(radiation3DHoverPoint)), Qt::NoButton,
        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(radiation3DSurface, &radiation3DHoverEvent);
    application.processEvents();
    const auto legendColorsDuringHover = legendColorCount(
        radiation3DSurface->grab(legendRegion).toImage());
    const auto pitchBeforeDrag = radiation3DSurface->property("viewPitch").toDouble();
    const auto dragStart = QPointF(radiation3DSurface->width() * 0.75,
        radiation3DSurface->height() * 0.75);
    QMouseEvent radiation3DPress(QEvent::MouseButtonPress, dragStart,
        QPointF(radiation3DSurface->mapToGlobal(dragStart.toPoint())), Qt::LeftButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(radiation3DSurface, &radiation3DPress);
    const auto dragEnd = dragStart - QPointF(0.0, 30.0);
    QMouseEvent radiation3DDrag(QEvent::MouseMove, dragEnd,
        QPointF(radiation3DSurface->mapToGlobal(dragEnd.toPoint())), Qt::NoButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(radiation3DSurface, &radiation3DDrag);
    QMouseEvent radiation3DRelease(QEvent::MouseButtonRelease, dragEnd,
        QPointF(radiation3DSurface->mapToGlobal(dragEnd.toPoint())), Qt::LeftButton,
        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(radiation3DSurface, &radiation3DRelease);
    application.processEvents();
    const auto verticalOrbitTracksPointer =
        radiation3DSurface->property("viewPitch").toDouble() > pitchBeforeDrag;
    const auto radiation3DProbeVisible = radiation3DHoverReadout->text().contains(
            QStringLiteral("Nearest NEC sample"))
        && radiation3DHoverReadout->text().contains(QStringLiteral("θ"))
        && radiation3DHoverReadout->text().contains(QStringLiteral("φ"))
        && radiation3DHoverReadout->text().contains(QStringLiteral("dBi"))
        && radiation3DSurface->property("hoverReadout").toString()
            == radiation3DHoverReadout->text();
    const auto radiation3DColorScaleVisible = radiation3DSurface->property(
            "gainColorScale").toBool()
        && radiation3DSurface->property("colorScaleMinimum").isValid()
        && radiation3DSurface->property("colorScaleMaximum").isValid()
        && radiation3DSurface->property("currentColorScale").toBool()
        && radiation3DSurface->property("currentColorScaleMinimum").toDouble() == 0.0
        && radiation3DSurface->property("currentColorScaleMaximum").toDouble() > 0.0
        && radiation3DSurface->property("currentColorScaleTop").toDouble() >= 100.0
        && legendColorsBeforeHover > 100
        && legendColorsDuringHover == legendColorsBeforeHover
        && verticalOrbitTracksPointer;
    radiation2DComponent->setCurrentIndex(1);
    radiation2D.setSelectedFrequency(14.2);
    radiation3D.setSelectedFrequency(14.2);
    radiation3DFloor->setCurrentIndex(4);
    application.processEvents();
    const auto radiationSweepSelectable = radiation2DFrequency->count() == 2
        && radiation2DFrequency->currentData().toDouble() == 14.2
        && radiation3DFrequency->currentData().toDouble() == 14.2
        && !radiation2DFrequency->isHidden() && !radiation3DFrequency->isHidden()
        && radiation2DFrequency->itemText(1).contains(QStringLiteral("2 pattern"));
    radiation2D.setSelectedFrequency(99.0);
    const auto nonPatternFrequencyIgnored = radiation2DFrequency->count() == 2
        && radiation2DFrequency->currentData().toDouble() == 14.2;
    radiation2DFrequency->setCurrentIndex(0);
    application.processEvents();
    const auto radiationFrequencySynchronized =
        radiation3DFrequency->currentData().toDouble() == 14.1;
    radiation2DFrequency->setCurrentIndex(1);
    application.processEvents();
    const auto mixedPatternCutsSeparated = radiation2DCutPlane->count() == 6
        && radiation2DCutPlane->itemData(0).toDouble() == 0.0
        && radiation2DCutPlane->itemData(1).toDouble() == 30.0
        && radiation2DDataset->count() == 2
        && radiation3DDataset->count() == 2
        && radiation3DDataset->currentData().toInt() == 0;
    radiation2DDataset->setCurrentIndex(radiation2DDataset->findData(1));
    application.processEvents();
    const auto horizontalDatasetSelectsCompatibleCut = !radiation2DOrientation->isEnabled()
        && radiation2DOrientation->text() == QStringLiteral("Horizontal Cut")
        && radiation2DCutPlane->count() == 1
        && radiation2DCutPlane->currentData().toDouble() == 62.0
        && radiation2DSummary->text().contains(QStringLiteral("peak"));
    radiation2DMaxGainCut->click();
    application.processEvents();
    const auto horizontalMaxGainCutStaysCompatible = radiation2DOrientation->text()
        == QStringLiteral("Horizontal Cut") && radiation2DCutPlane->currentData().toDouble() == 62.0;
    radiation2DDataset->setCurrentIndex(radiation2DDataset->findData(0));
    application.processEvents();
    const auto radiationControlsSynchronized = radiation3DComponent->currentData()
            == radiation2DComponent->currentData()
        && radiationPerformanceComponent->currentData() == radiation2DComponent->currentData()
        && radiation2DFloor->currentData() == radiation3DFloor->currentData();
    const auto radiationMetricsVisible = radiation2DSummary->text().contains(QStringLiteral("HPBW"))
        && radiation3DSummary->text().contains(QStringLiteral("peak"))
        && radiation3DProbeVisible && radiation3DColorScaleVisible
        && radiationPerformanceSummary->text().contains(QStringLiteral("Gain 2/2"))
        && radiationPerformanceSummary->text().contains(QStringLiteral("F/B 2/2"))
        && radiationPerformancePlot->property("pointCount").toInt() == 2
        && radiationPerformancePlot->property("seriesCount").toInt() == 3
        && radiationPerformancePlot->property("seriesStyles").toStringList()
            == QStringList{QString::number(static_cast<int>(Qt::SolidLine)),
                QString::number(static_cast<int>(Qt::SolidLine)),
                QString::number(static_cast<int>(Qt::DashLine))}
        && radiationPerformancePlot->property("selectedFrequencyMHz").toDouble() == 14.2;
    const auto radiationExportReady = radiation2DExportImage->isEnabled()
        && radiation2DExportData->isEnabled() && radiation3DExportImage->isEnabled()
        && radiation3DExportData->isEnabled();
    radiation2DOrientation->click();
    radiation2DMaxGainCut->click();
    application.processEvents();
    const auto maxGainCutSelected = radiation2DOrientation->text() == QStringLiteral("Vertical Cut")
        && radiation2DCutPlane->currentData().toDouble() == 60.0
        && radiation2DSummary->text().contains(QStringLiteral("20.000 dBi"));
    necwb::analysis::AnalysisResult signedThetaResult;
    for (auto theta = -90; theta <= 90; theta += 10) {
        for (const auto phi : {0.0, 180.0}) {
            const auto gain = theta == -70 ? 8.2 : -20.0;
            signedThetaResult.radiation.push_back({14.3, static_cast<double>(theta), phi,
                gain, -999.99, gain});
        }
    }
    necwb::ui::RadiationPatternView signedThetaView;
    signedThetaView.setResults(signedThetaResult, QStringLiteral("signed-theta-test"));
    auto* signedThetaSummary = signedThetaView.findChild<QLabel*>(
        QStringLiteral("radiation2DSummary"));
    const auto signedThetaTiesNormalized = signedThetaSummary != nullptr
        && signedThetaSummary->text().contains(QStringLiteral("at -70.000°"))
        && signedThetaSummary->text().contains(QStringLiteral("tied at 70.000°"))
        && !signedThetaSummary->text().contains(QStringLiteral("430"));
    auto* signedThetaPlot = signedThetaView.findChild<QWidget*>(
        QStringLiteral("radiationPolarPlot"));
    const auto signedThetaCutStaysOpen = signedThetaPlot != nullptr
        && !signedThetaPlot->property("closedPattern").toBool();
    necwb::ui::RadiationPatternView emptyRadiation2D;
    emptyRadiation2D.setResults({}, QStringLiteral("empty-radiation-test"));
    auto* emptyRadiationFrequency = emptyRadiation2D.findChild<QComboBox*>(
        QStringLiteral("radiation2DFrequency"));
    auto* emptyRadiationSummary = emptyRadiation2D.findChild<QLabel*>(
        QStringLiteral("radiation2DSummary"));
    auto* emptyRadiationExport = emptyRadiation2D.findChild<QPushButton*>(
        QStringLiteral("radiation2DExportData"));
    const auto missingRadiationReported = emptyRadiationFrequency != nullptr
        && emptyRadiationFrequency->count() == 0 && emptyRadiationSummary != nullptr
        && emptyRadiationSummary->text().contains(QStringLiteral("No"))
        && emptyRadiationExport != nullptr && !emptyRadiationExport->isEnabled();
    currents.show(); radiation2D.show(); radiation3D.show(); application.processEvents();
    QImage fieldImage(800, 600, QImage::Format_ARGB32_Premultiplied);
    fieldImage.fill(Qt::transparent); QPainter fieldPainter(&fieldImage);
    currents.render(&fieldPainter); radiation2D.render(&fieldPainter); radiation3D.render(&fieldPainter);
    fieldPainter.end();
    necwb::analysis::AnalysisResult currentOnly;
    currentOnly.currents = result.currents;
    radiation3D.setResults(currentOnly, QStringLiteral("Model: test.nec · Run: current-only"));
    radiation3D.setSelectedFrequency(14.1);
    application.processEvents();
    const auto missingRadiationDisablesDataExport = !radiation3DExportData->isEnabled()
        && radiation3DExportImage->isEnabled();
    QImage currentImage(800, 600, QImage::Format_ARGB32_Premultiplied);
    currentImage.fill(Qt::transparent); QPainter currentPainter(&currentImage);
    radiation3D.render(&currentPainter); currentPainter.end();
    QTextDocument sourceDocument(QStringLiteral("CM dashboard test\nCE\nGW 1 11 0 0 -0.5 0 0 0.5 0.001\nGE 0\nEN\n"));
    necwb::ui::DashboardPage dashboard(&sourceDocument);
    dashboard.resize(1000, 700);
    necwb::model::ModelSetup setup;
    dashboard.setModel(antenna, setup, QStringLiteral("nec2"), true, 0, 0);
    QAction checkAction(QStringLiteral("Check Model"), &dashboard);
    QAction runAction(QStringLiteral("Run Analysis"), &dashboard);
    QAction averageGainAction(QStringLiteral("Run Average Gain Test"), &dashboard);
    QAction convergenceAction(QStringLiteral("Segmentation Convergence"), &dashboard);
    dashboard.setQuickActions(&checkAction, &runAction);
    dashboard.setAverageGainAction(&averageGainAction);
    dashboard.setConvergenceAction(&convergenceAction);
    dashboard.setDocumentState(QStringLiteral("test.nec"), true);
    dashboard.setResults(result, QStringLiteral("Model: test.nec · Run: dashboard"), false);
    dashboard.setAverageGainResult(necwb::analysis::assessAverageGain(1.02, 1.0), 14.1);
    dashboard.setConvergenceState(QStringLiteral("Stable across final refinements"));
    dashboard.show(); application.processEvents();
    QImage dashboardImage(1000, 700, QImage::Format_ARGB32_Premultiplied);
    dashboardImage.fill(Qt::transparent); QPainter dashboardPainter(&dashboardImage);
    dashboard.render(&dashboardPainter); dashboardPainter.end();
    const auto* dashboardFileState = dashboard.findChild<QLabel*>(QStringLiteral("dashboardFileState"));
    const auto dashboardStateVisible = dashboardFileState != nullptr
        && dashboardFileState->text().contains(QStringLiteral("test.nec"))
        && dashboardFileState->text().contains(QStringLiteral("Unsaved"));
    const auto* dashboard3DControls = dashboard.findChild<QWidget*>(
        QStringLiteral("radiation3DControls"));
    const auto* dashboard3DSummary = dashboard.findChild<QLabel*>(
        QStringLiteral("radiation3DSummary"));
    const auto* dashboardQuickActionsContainer = dashboard.findChild<QWidget*>(
        QStringLiteral("dashboardQuickActionsContainer"));
    const auto* dashboardNecSourcePanel = dashboard.findChild<QGroupBox*>(
        QStringLiteral("dashboardNecSourcePanel"));
    const auto* dashboard3DOverviewPanel = dashboard.findChild<QGroupBox*>(
        QStringLiteral("dashboard3DOverviewPanel"));
    const auto* dashboardAverageGainState = dashboard.findChild<QLabel*>(
        QStringLiteral("dashboardAverageGainState"));
    const auto* dashboardConvergenceState = dashboard.findChild<QLabel*>(
        QStringLiteral("dashboardConvergenceState"));
    const auto* dashboardAverageGainButton = dashboard.findChild<QToolButton*>(
        QStringLiteral("dashboardAverageGainButton"));
    const auto* dashboardConvergenceButton = dashboard.findChild<QToolButton*>(
        QStringLiteral("dashboardConvergenceButton"));
    const auto dashboardQuickActions = dashboard.findChildren<QToolButton*>(
        QStringLiteral("dashboardQuickAction"));
    const auto dashboard3DIsOverview = dashboard3DControls != nullptr
        && dashboard3DControls->isHidden() && dashboard3DSummary != nullptr
        && dashboard3DSummary->isHidden();
    const auto quickActionsRightAligned = dashboardQuickActionsContainer != nullptr
        && dashboardQuickActionsContainer->parentWidget() != nullptr
        && dashboardQuickActionsContainer->parentWidget()->width()
            - dashboardQuickActionsContainer->x() - dashboardQuickActionsContainer->width() <= 12;
    const auto dashboardGroups = dashboard.findChildren<QGroupBox*>();
    const auto groupWithTitle = [&dashboardGroups](const QString& title) -> QGroupBox* {
        const auto match = std::ranges::find(dashboardGroups, title, &QGroupBox::title);
        return match == dashboardGroups.end() ? nullptr : *match;
    };
    const auto* dashboardModelSummaryPanel = groupWithTitle(QStringLiteral("Model Summary"));
    const auto* dashboardQuickResultsPanel = groupWithTitle(QStringLiteral("Quick Results"));
    const auto quadrantHeadersMatch = dashboardNecSourcePanel != nullptr
        && dashboard3DOverviewPanel != nullptr && dashboardModelSummaryPanel != nullptr
        && dashboardQuickResultsPanel != nullptr
        && dashboardNecSourcePanel->mapTo(&dashboard, QPoint{}).y()
            == dashboard3DOverviewPanel->mapTo(&dashboard, QPoint{}).y()
        && dashboardNecSourcePanel->font() == dashboard3DOverviewPanel->font()
        && dashboardNecSourcePanel->font() == dashboardModelSummaryPanel->font()
        && dashboardNecSourcePanel->font() == dashboardQuickResultsPanel->font()
        && dashboardQuickActionsContainer != nullptr
        && dashboardQuickActionsContainer->y() <= 2;
    const auto qualityStatesShareRow = dashboardAverageGainState != nullptr
        && dashboardConvergenceState != nullptr
        && dashboardAverageGainState->mapTo(&dashboard, QPoint{}).y()
            == dashboardConvergenceState->mapTo(&dashboard, QPoint{}).y();
    const auto quickActionsMatchQualityButtons = dashboardQuickActions.size() == 2
        && dashboardAverageGainButton != nullptr && dashboardConvergenceButton != nullptr
        && !dashboardQuickActions[0]->autoRaise() && !dashboardQuickActions[1]->autoRaise()
        && dashboardQuickActions[0]->toolButtonStyle()
            == dashboardAverageGainButton->toolButtonStyle()
        && dashboardQuickActions[1]->toolButtonStyle()
            == dashboardConvergenceButton->toolButtonStyle();
    if (dashboardQuickActions.size() != 2
        || dashboardQuickActions[0]->defaultAction() != &checkAction
        || dashboardQuickActions[1]->defaultAction() != &runAction
        || !quickActionsRightAligned || !quadrantHeadersMatch || !qualityStatesShareRow
        || !quickActionsMatchQualityButtons) {
        return NECWB_SMOKE_FAILURE("dashboard workspace");
    }
    QSettings{}.remove(QStringLiteral("resultWindows/smoke-test"));
    necwb::ui::DetachablePanel detachablePanel(
        QStringLiteral("smoke-test"), QStringLiteral("Smoke Results"));
    auto* detachableContent = new QLabel(QStringLiteral("Live result content"));
    detachablePanel.setContent(detachableContent);
    detachablePanel.show();
    application.processEvents();
    auto* popOutButton = detachablePanel.findChild<QPushButton*>(
        QStringLiteral("smoke-testPopOutButton"));
    if (popOutButton == nullptr) return NECWB_SMOKE_FAILURE("detachable panel button");
    popOutButton->click();
    application.processEvents();
    auto* resultWindow = detachablePanel.findChild<QDialog*>(
        QStringLiteral("smoke-testResultWindow"));
    const auto resultPanelDetached = detachablePanel.isDetached()
        && resultWindow != nullptr && resultWindow->isVisible()
        && detachableContent->window() == resultWindow;
    resultWindow->close();
    application.processEvents();
    const auto resultPanelReattached = !detachablePanel.isDetached()
        && detachableContent->parentWidget() == &detachablePanel;
    QSettings{}.remove(QStringLiteral("resultWindows/smoke-test"));
    QAction welcomeNew(QStringLiteral("New NEC Model"));
    QAction welcomeOpen(QStringLiteral("Open NEC File"));
    QString recentOpened;
    auto examplesOpened = false;
    auto recentCleared = false;
    necwb::ui::WelcomePage welcome(&welcomeNew, &welcomeOpen,
        [&recentOpened](const QString& path) { recentOpened = path; },
        [&examplesOpened] { examplesOpened = true; },
        [&recentCleared] { recentCleared = true; });
    welcome.resize(900, 650);
    welcome.setRecentFiles({QStringLiteral("/tmp/first.nec"), QStringLiteral("/tmp/second.nec")});
    welcome.show(); application.processEvents();
    auto* recentList = welcome.findChild<QListWidget*>(QStringLiteral("recentModelsList"));
    auto* openRecent = welcome.findChild<QPushButton*>(QStringLiteral("openRecentModelButton"));
    auto* clearRecent = welcome.findChild<QPushButton*>(QStringLiteral("clearRecentModelsButton"));
    if (recentList == nullptr || openRecent == nullptr || clearRecent == nullptr)
        return NECWB_SMOKE_FAILURE("welcome recent files controls");
    recentList->setCurrentRow(1); openRecent->click(); clearRecent->click();
    QImage welcomeImage(900, 650, QImage::Format_ARGB32_Premultiplied);
    welcomeImage.fill(Qt::transparent); QPainter welcomePainter(&welcomeImage);
    welcome.render(&welcomePainter); welcomePainter.end();
    const auto welcomeStateValid = recentList->count() == 2
        && recentOpened == QStringLiteral("/tmp/second.nec") && recentCleared;
    necwb::ui::StructuredCardEditor structuredCards;
    structuredCards.resize(900, 500);
    structuredCards.setDocument(necwb::nec::NecParser{}.parse(
        "GW 1 11 0 0 -0.5 0 0 0.5 0.001\nEX 0 1 6 0 1 0\nFR 0 1 0 0 14.1 0\nEN\n"));
    QString editedCard;
    QString addedCard;
    std::size_t deletedLine{};
    QObject::connect(&structuredCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&editedCard](std::size_t, const QString& text) { editedCard = text; });
    QObject::connect(&structuredCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&structuredCards](std::size_t, const QString& text) {
            structuredCards.setDocument(necwb::nec::NecParser{}.parse(
                "GW 1 11 0 0 -0.5 0 0 0.5 0.001\nGE 0\n"
                + text.toStdString() + "\nFR 0 1 0 0 14.1 0\nEN\n"));
        });
    QObject::connect(&structuredCards, &necwb::ui::StructuredCardEditor::cardAddRequested,
        [&addedCard](const QString& text) { addedCard = text; });
    QObject::connect(&structuredCards, &necwb::ui::StructuredCardEditor::cardDeleteRequested,
        [&deletedLine](std::size_t sourceLine) { deletedLine = sourceLine; });
    auto* structuredTable = structuredCards.findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
    auto* structuredFamilies = structuredCards.findChild<QTreeWidget*>(
        QStringLiteral("structuredCardFamilies"));
    auto* structuredAdd = structuredCards.findChild<QPushButton*>(
        QStringLiteral("structuredAddCardButton"));
    const auto selectStructuredFamily = [](QTreeWidget* tree, const QString& prefix) {
        QTreeWidgetItemIterator item(tree);
        while (*item != nullptr) {
            if ((*item)->childCount() == 0 && (*item)->text(0).startsWith(prefix)) {
                tree->setCurrentItem(*item);
                return true;
            }
            ++item;
        }
        return false;
    };
    if (structuredTable == nullptr || structuredFamilies == nullptr || structuredAdd == nullptr
        || !selectStructuredFamily(structuredFamilies, QStringLiteral("EX")))
        return NECWB_SMOKE_FAILURE("structured card controls");
    application.processEvents();
    const auto structuredHierarchyValid = structuredFamilies->topLevelItemCount() == 6
        && structuredFamilies->topLevelItem(0)->text(0) == QStringLiteral("Geometry")
        && structuredFamilies->topLevelItem(3)->text(0) == QStringLiteral("Loads & Networks")
        && structuredFamilies->topLevelItem(4)->childCount() == 4;
    if (structuredTable->rowCount() != 1 || !structuredAdd->isEnabled())
        return NECWB_SMOKE_FAILURE("structured card family selection");
    const auto structuredSelectionWorks = structuredCards.selectCard(3)
        && structuredFamilies->currentItem()->text(0).startsWith(QStringLiteral("FR"))
        && structuredTable->currentRow() == 0;
    application.processEvents();
    const auto duplicateFrequencyBlocked = !structuredAdd->isEnabled();
    selectStructuredFamily(structuredFamilies, QStringLiteral("EX"));
    application.processEvents();
    structuredTable->item(0, 4)->setText(QStringLiteral("not-an-integer"));
    application.processEvents();
    const auto invalidEditBlocked = editedCard.isEmpty()
        && !structuredTable->item(0, 4)->toolTip().isEmpty();
    structuredTable->item(0, 4)->setText(QStringLiteral("7"));
    structuredCards.show(); application.processEvents();
    structuredTable->item(0, 8)->setText(QStringLiteral("2.5"));
    application.processEvents();
    selectStructuredFamily(structuredFamilies, QStringLiteral("LD"));
    application.processEvents();
    structuredAdd->click();
    application.processEvents();
    selectStructuredFamily(structuredFamilies, QStringLiteral("EX"));
    application.processEvents();
    structuredTable->selectRow(0);
    structuredCards.findChild<QPushButton*>(QStringLiteral("structuredDeleteCardButton"))->click();
    application.processEvents();
    structuredTable->openPersistentEditor(structuredTable->item(0, 2));
    application.processEvents();
    const auto cardDropdowns = structuredTable->findChildren<QComboBox*>();
    const auto descriptiveDropdown = cardDropdowns.size() == 1
        && cardDropdowns.front()->currentText().contains(QStringLiteral("Applied voltage source"))
        && cardDropdowns.front()->count() == 6;
    structuredTable->closePersistentEditor(structuredTable->item(0, 2));
    QImage structuredImage(900, 500, QImage::Format_ARGB32_Premultiplied);
    structuredImage.fill(Qt::transparent); QPainter structuredPainter(&structuredImage);
    structuredCards.render(&structuredPainter); structuredPainter.end();
    necwb::ui::StructuredCardEditor extendedCards;
    extendedCards.setDocument(necwb::nec::NecParser{}.parse(
        "FR 0 101 0 0 3.0 0.05 8.0 0 0 0\n"));
    auto* extendedFamilies = extendedCards.findChild<QTreeWidget*>(
        QStringLiteral("structuredCardFamilies"));
    auto* extendedTable = extendedCards.findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
    QString extendedFrequency;
    QObject::connect(&extendedCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&extendedFrequency](std::size_t, const QString& text) { extendedFrequency = text; });
    selectStructuredFamily(extendedFamilies, QStringLiteral("FR"));
    application.processEvents();
    extendedTable->item(0, 6)->setText(QStringLiteral("3.1"));
    application.processEvents();
    const auto trailingFieldsPreserved = extendedFrequency
        == QStringLiteral("FR 0 101 0 0 3.1 0.05 8.0 0 0 0");
    necwb::ui::StructuredCardEditor generatedGeometryCards;
    generatedGeometryCards.setDeckUnitLabel(QStringLiteral("m"));
    generatedGeometryCards.setDocument(necwb::nec::NecParser{}.parse(
        "GA 1 9 0.5 0 180 0.001\n"
        "GH 2 80 0.03 0.30 0.02 0.02 0.02 0.02 0.001\nGE 0\n"));
    auto* generatedGeometryFamilies = generatedGeometryCards.findChild<QTreeWidget*>(
        QStringLiteral("structuredCardFamilies"));
    auto* generatedGeometryTable = generatedGeometryCards.findChild<QTableWidget*>(
        QStringLiteral("structuredCardTable"));
    auto* generatedGeometryAdd = generatedGeometryCards.findChild<QPushButton*>(
        QStringLiteral("structuredAddCardButton"));
    QString editedArc;
    QString addedGeneratedGeometry;
    QObject::connect(&generatedGeometryCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&editedArc](std::size_t, const QString& text) { editedArc = text; });
    QObject::connect(&generatedGeometryCards, &necwb::ui::StructuredCardEditor::cardAddRequested,
        [&addedGeneratedGeometry](const QString& text) { addedGeneratedGeometry = text; });
    const auto arcFamilyFound = selectStructuredFamily(
        generatedGeometryFamilies, QStringLiteral("GA"));
    application.processEvents();
    const auto arcFieldsLabeled = arcFamilyFound && generatedGeometryTable->rowCount() == 1
        && generatedGeometryTable->columnCount() == 8
        && generatedGeometryTable->horizontalHeaderItem(4)->text().contains(QStringLiteral("Arc Radius"))
        && generatedGeometryTable->horizontalHeaderItem(5)->text().contains(QStringLiteral("Start Angle"))
        && generatedGeometryTable->item(0, 4)->toolTip().contains(QStringLiteral("deck length"));
    generatedGeometryTable->item(0, 6)->setText(QStringLiteral("170"));
    application.processEvents();
    const auto arcEditCommitted = editedArc == QStringLiteral("GA 1 9 0.5 0 170 0.001");
    const auto helixFamilyFound = selectStructuredFamily(
        generatedGeometryFamilies, QStringLiteral("GH"));
    application.processEvents();
    const auto helixFieldsLabeled = helixFamilyFound && generatedGeometryTable->rowCount() == 1
        && generatedGeometryTable->columnCount() == 11
        && generatedGeometryTable->horizontalHeaderItem(4)->text() == QStringLiteral("Turn Spacing (m)")
        && generatedGeometryTable->horizontalHeaderItem(5)->text() == QStringLiteral("Axial Length (m)")
        && generatedGeometryTable->item(0, 5)->toolTip().contains(QStringLiteral("spiral"));
    generatedGeometryAdd->click();
    application.processEvents();
    const auto helixDefaultValid = addedGeneratedGeometry.startsWith(QStringLiteral("GH 3 "));
    selectStructuredFamily(generatedGeometryFamilies, QStringLiteral("Other NEC-2 Geometry"));
    application.processEvents();
    const auto generatedCardsExcludedFromOther = generatedGeometryTable->rowCount() == 0;
    necwb::ui::WireCardEditor wireEditor;
    wireEditor.resize(900, 400);
    necwb::model::AntennaModel wireModel;
    wireModel.addWire({1, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 11, 0.001, 1});
    wireEditor.setModel(wireModel);
    auto wireEditCommitted = false;
    auto structuredGaugeRadius = 0.0;
    QObject::connect(&wireEditor, &necwb::ui::WireCardEditor::wireEdited,
        [&wireEditor, &wireEditCommitted, &structuredGaugeRadius](
            const necwb::model::Wire&, const necwb::model::Wire& updated) {
            necwb::model::AntennaModel refreshed;
            refreshed.addWire(updated);
            wireEditor.setModel(refreshed);
            wireEditCommitted = true;
            structuredGaugeRadius = updated.radius;
        });
    auto* wireTable = wireEditor.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
    if (wireTable == nullptr || wireTable->rowCount() != 1)
        return NECWB_SMOKE_FAILURE("wire card rows");
    auto* structuredGauge = qobject_cast<QComboBox*>(wireTable->cellWidget(0, 9));
    if (structuredGauge == nullptr) return NECWB_SMOKE_FAILURE("wire gauge editor");
    auto structuredGaugeRangeValid = structuredGauge->count() == 22;
    for (auto index = 1; index < structuredGauge->count(); ++index)
        structuredGaugeRangeValid = structuredGaugeRangeValid
            && structuredGauge->itemData(index).toInt() == index + 9;
    structuredGauge->setCurrentIndex(structuredGauge->findData(20));
    wireEditor.show(); application.processEvents();
    const auto structuredGaugeCommitted = std::abs(
        structuredGaugeRadius - necwb::model::awgRadiusMeters(20)) < 1.0e-12;
    wireTable->item(0, 8)->setText(QStringLiteral("0.002"));
    application.processEvents();
    auto* customGauge = qobject_cast<QComboBox*>(wireTable->cellWidget(0, 9));
    const auto customRadiusPreserved = customGauge != nullptr && customGauge->currentIndex() == 0;
    necwb::ui::WireCardEditor symbolicWireEditor;
    symbolicWireEditor.setSymbolicGeometryFields({{1, {3, 4, 6, 7}}});
    symbolicWireEditor.setParameterControlledFields({{1, {{3, "height"}}}});
    symbolicWireEditor.setModel(wireModel);
    auto* symbolicWireTable = symbolicWireEditor.findChild<QTableWidget*>(
        QStringLiteral("wireCardTable"));
    auto* symbolicGauge = symbolicWireTable == nullptr ? nullptr
        : qobject_cast<QComboBox*>(symbolicWireTable->cellWidget(0, 9));
    const auto symbolicCoordinatesLocked = symbolicWireTable != nullptr
        && !(symbolicWireTable->item(0, 3)->flags() & Qt::ItemIsEditable)
        && (symbolicWireTable->item(0, 8)->flags() & Qt::ItemIsEditable)
        && !symbolicWireTable->item(0, 3)->icon().isNull()
        && symbolicWireTable->item(0, 3)->font().italic()
        && symbolicWireTable->item(0, 3)->toolTip().contains(QStringLiteral("ƒx"));
    const auto symbolicGaugeEnabled = symbolicGauge != nullptr && symbolicGauge->isEnabled();
    QImage wireImage(900, 400, QImage::Format_ARGB32_Premultiplied);
    wireImage.fill(Qt::transparent); QPainter wirePainter(&wireImage);
    wireEditor.render(&wirePainter); wirePainter.end();
    necwb::model::AntennaModel gaugeModel;
    const auto gaugeRadius = necwb::model::awgRadiusMeters(12);
    const necwb::model::Wire gaugeWire{2, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
        11, gaugeRadius, 1};
    gaugeModel.addWire(gaugeWire);
    necwb::ui::WirePropertiesDialog wireProperties(gaugeWire, gaugeModel,
        necwb::model::LengthUnit::Meter);
    auto* gaugeEditor = wireProperties.findChild<QComboBox*>(QStringLiteral("wireGaugeDialogEditor"));
    const auto gaugeDropdownValid = gaugeEditor != nullptr
        && gaugeEditor->currentData().toInt() == 12;
    QTabWidget setupHost;
    necwb::ui::SetupEditor setupEditor(&setupHost);
    setupHost.addTab(setupEditor.sourcesPage(), QStringLiteral("Sources"));
    setupHost.addTab(setupEditor.environmentPage(), QStringLiteral("Environment"));
    setupHost.addTab(setupEditor.frequencyPage(), QStringLiteral("Frequency"));
    setupHost.resize(900, 650);
    setupHost.show();
    application.processEvents();
    const auto setupColumnsValid = setupHost.count() == 3
        && !setupEditor.isVisible()
        && setupEditor.sourcesPage()->objectName() == QStringLiteral("sourcesEditorPage")
        && setupEditor.environmentPage()->objectName() == QStringLiteral("environmentEditorPage")
        && setupEditor.frequencyPage()->objectName() == QStringLiteral("frequencyEditorPage");
    auto* groundPreset = setupEditor.environmentPage()->findChild<QComboBox*>(
        QStringLiteral("groundPresetControl"));
    auto* groundPermittivity = setupEditor.environmentPage()->findChild<QDoubleSpinBox*>(
        QStringLiteral("groundRelativePermittivity"));
    auto* groundConductivity = setupEditor.environmentPage()->findChild<QDoubleSpinBox*>(
        QStringLiteral("groundConductivity"));
    auto* sourceReferenceImpedance = setupEditor.sourcesPage()->findChild<QDoubleSpinBox*>(
        QStringLiteral("sourceReferenceImpedance"));
    auto* applyReferenceImpedance = setupEditor.sourcesPage()->findChild<QPushButton*>(
        QStringLiteral("applyReferenceImpedanceButton"));
    if (groundPreset == nullptr || groundPermittivity == nullptr
        || groundConductivity == nullptr || sourceReferenceImpedance == nullptr
        || applyReferenceImpedance == nullptr)
        return NECWB_SMOKE_FAILURE("model setup controls");
    auto referenceImpedanceSignalValid = false;
    QObject::connect(&setupEditor, &necwb::ui::SetupEditor::referenceImpedanceChanged,
        [&referenceImpedanceSignalValid](const auto& reference) {
            referenceImpedanceSignalValid = reference.sourceLine == 0
                && std::abs(reference.ohms - 200.0) < 1.0e-12;
        });
    sourceReferenceImpedance->setValue(200.0);
    const auto referenceImpedancePending = setupEditor.hasPendingEdits(
        setupEditor.sourcesPage())
        && applyReferenceImpedance->property("pendingChanges").toBool();
    applyReferenceImpedance->click();
    const auto referenceImpedanceControlValid = referenceImpedanceSignalValid
        && !setupEditor.hasPendingEdits(setupEditor.sourcesPage());
    const auto saltWaterIndex = groundPreset->findText(
        QStringLiteral("Salt water"), Qt::MatchStartsWith);
    groundPreset->setCurrentIndex(saltWaterIndex);
    const auto groundPresetValuesValid = groundPreset->count() == 10
        && saltWaterIndex > 0
        && std::abs(groundPermittivity->value() - 81.0) < 1.0e-9
        && std::abs(groundConductivity->value() - 5.0) < 1.0e-12;
    groundConductivity->setValue(4.5);
    const auto customGroundDetected = groundPreset->currentIndex() == 0;
    auto* applyGround = setupEditor.environmentPage()->findChild<QPushButton*>(
        QStringLiteral("applyGroundButton"));
    const auto groundApplyHighlighted = applyGround != nullptr
        && applyGround->property("pendingChanges").toBool()
        && setupEditor.hasPendingEdits(setupEditor.environmentPage());
    setupEditor.discardPendingEdits(setupEditor.environmentPage());
    const auto groundDiscardRestored = !setupEditor.hasPendingEdits(
        setupEditor.environmentPage())
        && std::abs(groundConductivity->value() - 0.005) < 1.0e-12;
    necwb::ui::ParameterEditor parameterEditor;
    parameterEditor.setResolution(necwb::nec::NecSymbolResolver{}.resolve(
        "SY LENGTH=10, HALF=LENGTH/2\nGW 1 11 -HALF 0 0 HALF 0 0 .001\nGE 0\nEN\n"));
    parameterEditor.resize(760, 520);
    parameterEditor.show();
    application.processEvents();
    auto* parameterTable = parameterEditor.findChild<QTableWidget*>(QStringLiteral("parameterTable"));
    auto* applyParameter = parameterEditor.findChild<QPushButton*>(
        QStringLiteral("applyParameterButton"));
    auto* revertParameter = parameterEditor.findChild<QPushButton*>(
        QStringLiteral("revertParameterButton"));
    auto parameterSignalValid = false;
    QObject::connect(&parameterEditor, &necwb::ui::ParameterEditor::parameterChanged,
        [&parameterSignalValid](std::size_t sourceLine, const QString& originalName,
            const QString& name, const QString& expression) {
            parameterSignalValid = sourceLine == 1 && originalName == QStringLiteral("HALF")
                && name == QStringLiteral("HALF") && expression == QStringLiteral("LENGTH/2+1");
        });
    if (parameterTable == nullptr || applyParameter == nullptr
        || revertParameter == nullptr)
        return NECWB_SMOKE_FAILURE("parameter editor controls");
    parameterTable->selectRow(1);
    application.processEvents();
    parameterTable->item(1, 1)->setText(QStringLiteral("LENGTH/2+1"));
    const auto parameterEditPending = parameterEditor.hasPendingEdits()
        && applyParameter->isEnabled() && revertParameter->isEnabled()
        && applyParameter->property("pendingChanges").toBool()
        && parameterTable->item(1, 2)->text() == QStringLiteral("Apply to resolve");
    applyParameter->click();
    revertParameter->click();
    const auto parameterRevertValid = !parameterEditor.hasPendingEdits()
        && parameterTable->item(1, 1)->text() == QStringLiteral("LENGTH/2")
        && parameterTable->item(1, 2)->text() == QStringLiteral("5.000");
    const auto parameterEditorValid = parameterTable->columnCount() == 3
        && parameterTable->rowCount() == 2
        && parameterTable->item(0, 0)->text() == QStringLiteral("LENGTH")
        && parameterTable->item(1, 2)->text() == QStringLiteral("5.000")
        && parameterTable->editTriggers().testFlag(QAbstractItemView::DoubleClicked)
        && !parameterTable->item(1, 2)->flags().testFlag(Qt::ItemIsEditable)
        && parameterEditPending && parameterRevertValid && parameterSignalValid;
    necwb::ui::ParameterEditor emptyParameterEditor;
    emptyParameterEditor.setResolution(necwb::nec::NecSymbolResolver{}.resolve(
        "GW 1 11 -1 0 0 1 0 0 .001\nGE 0\nEN\n"));
    auto* emptyParameterTable = emptyParameterEditor.findChild<QTableWidget*>(
        QStringLiteral("parameterTable"));
    auto* addParameter = emptyParameterEditor.findChild<QPushButton*>(
        QStringLiteral("addParameterButton"));
    auto* applyNewParameter = emptyParameterEditor.findChild<QPushButton*>(
        QStringLiteral("applyParameterButton"));
    auto newParameterSignalValid = false;
    QObject::connect(&emptyParameterEditor, &necwb::ui::ParameterEditor::parameterChanged,
        [&newParameterSignalValid](std::size_t sourceLine, const QString& originalName,
            const QString& name, const QString& expression) {
            newParameterSignalValid = sourceLine == 0 && originalName.isEmpty()
                && name == QStringLiteral("LENGTH") && expression == QStringLiteral("10");
        });
    if (emptyParameterTable == nullptr || addParameter == nullptr
        || applyNewParameter == nullptr || !addParameter->isEnabled())
        return NECWB_SMOKE_FAILURE("new parameter controls");
    addParameter->click();
    emptyParameterTable->item(0, 0)->setText(QStringLiteral("LENGTH"));
    emptyParameterTable->item(0, 1)->setText(QStringLiteral("10"));
    const auto emptyParameterDraftValid = emptyParameterTable->rowCount() == 1
        && applyNewParameter->isEnabled() && emptyParameterEditor.hasPendingEdits();
    applyNewParameter->click();
    const auto newParameterValid = emptyParameterDraftValid && newParameterSignalValid;
    necwb::model::ModelSetup requestSetup;
    requestSetup.frequency = necwb::model::FrequencyDefinition{0, 1, 14.15, 0.0, 4};
    requestSetup.radiationPatterns = {
        {37, 36, 0.0, 0.0, 5.0, 10.0, 5},
        {1, 360, 62.0, 0.0, 0.0, 1.0, 6}};
    necwb::ui::AnalysisRequestEditor requestEditor;
    requestEditor.setData(requestSetup);
    requestEditor.resize(900, 760);
    requestEditor.show();
    application.processEvents();
    auto* requestTable = requestEditor.findChild<QTableWidget*>(
        QStringLiteral("radiationRequestsTable"));
    auto* addPattern = requestEditor.findChild<QPushButton*>(
        QStringLiteral("addRadiationPatternButton"));
    auto* applyPattern = requestEditor.findChild<QPushButton*>(
        QStringLiteral("applyRadiationPatternButton"));
    auto* patternType = requestEditor.findChild<QComboBox*>(
        QStringLiteral("radiationPatternType"));
    auto* resetPattern = requestEditor.findChild<QPushButton*>(
        QStringLiteral("resetRadiationPatternButton"));
    auto* thetaStart = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationThetaStart"));
    auto* thetaEnd = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationThetaEnd"));
    auto* phiEnd = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationPhiEnd"));
    auto* frequencyPolicyGroup = requestEditor.findChild<QGroupBox*>(
        QStringLiteral("radiationFrequencyPolicyGroup"));
    auto* patternFrequencyMode = requestEditor.findChild<QComboBox*>(
        QStringLiteral("radiationFrequencyMode"));
    auto* patternSingleFrequency = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationSingleFrequency"));
    auto* patternContinuousStart = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationContinuousStart"));
    auto* patternContinuousStop = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationContinuousStop"));
    auto* patternContinuousStep = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationContinuousStep"));
    auto* patternSelectedFrequencies = requestEditor.findChild<QListWidget*>(
        QStringLiteral("radiationSelectedFrequencies"));
    auto* addPatternBandCenters = requestEditor.findChild<QPushButton*>(
        QStringLiteral("radiationAddAmateurBandCenters"));
    auto* applyPatternFrequencies = requestEditor.findChild<QPushButton*>(
        QStringLiteral("applyRadiationFrequenciesButton"));
    auto* clearPatternFrequencies = requestEditor.findChild<QPushButton*>(
        QStringLiteral("radiationClearFrequencies"));
    auto* selectedPatternGroup = requestEditor.findChild<QGroupBox*>(
        QStringLiteral("selectedRadiationPatternGroup"));
    auto* thetaGroup = requestEditor.findChild<QGroupBox*>(QStringLiteral("radiationThetaGroup"));
    auto* phiGroup = requestEditor.findChild<QGroupBox*>(QStringLiteral("radiationPhiGroup"));
    if (requestTable == nullptr || addPattern == nullptr || applyPattern == nullptr
        || patternType == nullptr || resetPattern == nullptr || thetaStart == nullptr
        || thetaEnd == nullptr || phiEnd == nullptr || frequencyPolicyGroup == nullptr
        || patternFrequencyMode == nullptr || patternSingleFrequency == nullptr
        || patternContinuousStart == nullptr || patternContinuousStop == nullptr
        || patternContinuousStep == nullptr || patternSelectedFrequencies == nullptr
        || addPatternBandCenters == nullptr || applyPatternFrequencies == nullptr
        || clearPatternFrequencies == nullptr
        || selectedPatternGroup == nullptr || thetaGroup == nullptr || phiGroup == nullptr)
        return NECWB_SMOKE_FAILURE("analysis request controls");
    const auto patternRequestLayoutValid = frequencyPolicyGroup->geometry().top()
            < requestTable->geometry().top()
        && thetaGroup->geometry().top() == phiGroup->geometry().top()
        && thetaGroup->geometry().left() < phiGroup->geometry().left();
    auto emittedPattern = necwb::model::RadiationPatternRequest{};
    QObject::connect(&requestEditor, &necwb::ui::AnalysisRequestEditor::patternChanged,
        [&emittedPattern](const auto& pattern) { emittedPattern = pattern; });
    requestTable->selectRow(1);
    application.processEvents();
    thetaStart->setValue(63.0);
    thetaEnd->setValue(63.0);
    const auto applyPatternHighlightsPending =
        applyPattern->property("pendingChanges").toBool();
    const auto customizedHorizontalRemainsHorizontal = patternType->currentData().toInt() == 1
        && resetPattern->isEnabled();
    resetPattern->click();
    const auto horizontalPresetRestored = thetaStart->value() == 90.0
        && thetaEnd->value() == 90.0 && patternType->currentData().toInt() == 1;
    requestTable->selectRow(0);
    application.processEvents();
    phiEnd->setValue(0.0);
    const auto unclassifiedGridBecomesCustom = patternType->currentData().toInt() == 3
        && resetPattern->isEnabled();
    resetPattern->click();
    const auto fullGridPresetRestored = patternType->currentData().toInt() == 0
        && phiEnd->value() == 350.0;
    addPattern->click();
    patternType->setCurrentIndex(patternType->findData(1));
    applyPattern->click();
    const auto applyPatternClearsPending =
        !applyPattern->property("pendingChanges").toBool();
    const auto patternEditorBaseValid = requestTable->rowCount() == 3
        && requestTable->item(0, 0)->text() == QStringLiteral("Full 3D pattern")
        && requestTable->item(1, 0)->text() == QStringLiteral("Horizontal cut")
        && emittedPattern.sourceLine == 0 && emittedPattern.thetaCount == 1
        && emittedPattern.phiCount == 360 && emittedPattern.phiStep == 1.0;
    patternFrequencyMode->setCurrentIndex(patternFrequencyMode->findData(3));
    patternContinuousStart->setValue(7.0);
    patternContinuousStop->setValue(7.2);
    patternContinuousStep->setValue(0.1);
    const auto frequencyApplyHighlightsPending =
        applyPatternFrequencies->property("pendingChanges").toBool()
        && requestEditor.hasPendingEdits();
    applyPatternFrequencies->click();
    const auto continuousPatternPlan = requestEditor.radiationFrequencyPlan();
    const auto continuousPatternPoints =
        necwb::analysis::frequencyPlanPoints(continuousPatternPlan);
    patternFrequencyMode->setCurrentIndex(patternFrequencyMode->findData(1));
    patternSingleFrequency->setValue(14.2);
    applyPatternFrequencies->click();
    const auto singlePatternPoints = necwb::analysis::frequencyPlanPoints(
        requestEditor.radiationFrequencyPlan());
    patternFrequencyMode->setCurrentIndex(patternFrequencyMode->findData(2));
    patternSelectedFrequencies->clear();
    QTimer::singleShot(0, &requestEditor, [&requestEditor] {
        auto* dialog = requestEditor.findChild<QDialog*>(
            QStringLiteral("radiationAmateurBandCenterDialog"));
        if (dialog == nullptr) return;
        auto* twentyMeters = dialog->findChild<QCheckBox*>(
            QStringLiteral("radiationAmateurBandCenterCheck4"));
        auto* addSelected = dialog->findChild<QPushButton*>(
            QStringLiteral("radiationAddSelectedBandCenters"));
        if (twentyMeters != nullptr) twentyMeters->setChecked(true);
        if (addSelected != nullptr) addSelected->click();
    });
    addPatternBandCenters->click();
    application.processEvents();
    const auto bandCentersPopulateList = patternSelectedFrequencies->count() == 1
        && patternSelectedFrequencies->item(0)->text().contains(QStringLiteral("14.175"))
        && patternSelectedFrequencies->viewMode() == QListView::IconMode
        && patternSelectedFrequencies->visualItemRect(
            patternSelectedFrequencies->item(0)).isValid()
        && applyPatternFrequencies->property("pendingChanges").toBool();
    applyPatternFrequencies->click();
    const auto bandCenterPoints = necwb::analysis::frequencyPlanPoints(
        requestEditor.radiationFrequencyPlan());
    clearPatternFrequencies->click();
    const auto clearAllEmptiesList = patternSelectedFrequencies->count() == 0
        && requestEditor.hasPendingEdits() && !applyPatternFrequencies->isEnabled();
    requestEditor.discardPendingEdits();
    const auto discardedFrequencyEditRestored = patternSelectedFrequencies->count() == 1
        && !requestEditor.hasPendingEdits();
    const auto patternFrequencySelectionValid = continuousPatternPoints.size() == 3
        && std::abs(continuousPatternPoints[1] - 7.1) < 1.0e-9
        && singlePatternPoints.size() == 1
        && std::abs(singlePatternPoints.front() - 14.2) < 1.0e-9
        && bandCenterPoints.size() == 1
        && std::abs(bandCenterPoints.front() - 14.175) < 1.0e-9;
    const auto multiplePatternEditorValid = patternEditorBaseValid && patternRequestLayoutValid
        && customizedHorizontalRemainsHorizontal && horizontalPresetRestored
        && unclassifiedGridBecomesCustom && fullGridPresetRestored
        && applyPatternHighlightsPending && applyPatternClearsPending
        && frequencyApplyHighlightsPending && bandCentersPopulateList
        && clearAllEmptiesList && discardedFrequencyEditRestored
        && patternFrequencySelectionValid;
    auto* shutdownRequestEditor = new necwb::ui::AnalysisRequestEditor;
    shutdownRequestEditor->setData(requestSetup);
    auto* shutdownFrequencyMode = shutdownRequestEditor->findChild<QComboBox*>(
        QStringLiteral("radiationFrequencyMode"));
    auto* shutdownFrequencyList = shutdownRequestEditor->findChild<QListWidget*>(
        QStringLiteral("radiationSelectedFrequencies"));
    if (shutdownFrequencyMode == nullptr || shutdownFrequencyList == nullptr)
        return NECWB_SMOKE_FAILURE("analysis request shutdown");
    shutdownFrequencyMode->setCurrentIndex(shutdownFrequencyMode->findData(2));
    shutdownFrequencyList->clear();
    delete shutdownRequestEditor;
    application.processEvents();
    necwb::model::AntennaModel attachmentModel;
    attachmentModel.addWire({1, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 11, 0.001, 1});
    attachmentModel.addWire({2, {-1.0, 1.0, 0.0}, {1.0, 1.0, 0.0}, 11, 0.001, 2});
    necwb::ui::AutoSegmentationDialog segmentationDialog(attachmentModel, {}, 30.0);
    auto* applySegmentation = segmentationDialog.findChild<QPushButton*>(
        QStringLiteral("applySegmentationButton"));
    if (applySegmentation == nullptr)
        return NECWB_SMOKE_FAILURE("automatic segmentation controls");
    applySegmentation->click();
    application.processEvents();
    const auto segmentationApplyAccepted = segmentationDialog.result() == QDialog::Accepted;
    necwb::model::ModelSetup attachmentSetup;
    attachmentSetup.loads.push_back({4, 1, 6, 6, 50.0, 0.0, 0.0, 7});
    attachmentSetup.transmissionLines.push_back({1, 3, 2, 9, 50.0, 3.048,
        0.0, 0.0, 0.0, 0.0, 8});
    necwb::ui::GeometryView attachment2D(necwb::geometry::ProjectionPlane::XY);
    necwb::ui::Geometry3DView attachment3D;
    attachment2D.resize(700, 450); attachment3D.resize(700, 450);
    attachment2D.setModel(attachmentModel); attachment3D.setModel(attachmentModel);
    attachment2D.setAttachments(attachmentSetup.loads, attachmentSetup.transmissionLines);
    attachment3D.setAttachments(attachmentSetup.loads, attachmentSetup.transmissionLines);
    attachment2D.setPendingTransmissionLineEndpoint(std::pair{1, 2});
    attachment3D.setPendingTransmissionLineEndpoint(std::pair{1, 2});
    attachment2D.selectLoad(7); attachment3D.selectTransmissionLine(8);
    attachment2D.show(); attachment3D.show(); application.processEvents();
    QImage attachmentImage(700, 450, QImage::Format_ARGB32_Premultiplied);
    attachmentImage.fill(Qt::transparent); QPainter attachmentPainter(&attachmentImage);
    attachment2D.render(&attachmentPainter); attachment3D.render(&attachmentPainter);
    attachmentPainter.end();
    necwb::ui::LoadNetworkEditor loadNetwork;
    loadNetwork.setData(attachmentModel, attachmentSetup);
    loadNetwork.setLengthUnit(necwb::model::LengthUnit::Foot);
    loadNetwork.show(); application.processEvents();
    auto* loadTable = loadNetwork.findChild<QTableWidget*>(QStringLiteral("loadNetworkLoadsTable"));
    auto* lineTable = loadNetwork.findChild<QTableWidget*>(QStringLiteral("loadNetworkLinesTable"));
    auto* addLoad = loadNetwork.findChild<QPushButton*>(QStringLiteral("addLoadButton"));
    auto* addLine = loadNetwork.findChild<QPushButton*>(QStringLiteral("addTransmissionLineButton"));
    auto* applyLoad = loadNetwork.findChild<QPushButton*>(QStringLiteral("applySelectedLoadButton"));
    auto* applyLine = loadNetwork.findChild<QPushButton*>(QStringLiteral("applySelectedLineButton"));
    auto* loadValidation = loadNetwork.findChild<QLabel*>(QStringLiteral("loadNetworkValidation"));
    auto* loadType = loadNetwork.findChild<QComboBox*>(QStringLiteral("loadTypeCombo"));
    if (loadTable == nullptr || lineTable == nullptr || addLoad == nullptr || addLine == nullptr
        || applyLoad == nullptr || applyLine == nullptr
        || loadValidation == nullptr || loadType == nullptr)
        return NECWB_SMOKE_FAILURE("loads and lines controls");
    if (loadTable->item(0, 5)->text() != QStringLiteral("50.000")
        || lineTable->item(0, 4)->text() != QStringLiteral("50.000")
        || lineTable->item(0, 5)->text() != QStringLiteral("10.000")
        || lineTable->horizontalHeaderItem(5)->text() != QStringLiteral("Length (ft)"))
        return NECWB_SMOKE_FAILURE("transmission-line columns");
    loadTable->selectRow(0);
    if (loadType->count() != 6 || loadType->currentData().toInt() != 4
        || loadTable->horizontalHeaderItem(5)->text() != QStringLiteral("Resistance (Ω)")
        || loadTable->horizontalHeaderItem(6)->text() != QStringLiteral("Reactance (Ω)")
        || loadTable->item(0, 7)->flags().testFlag(Qt::ItemIsEditable))
        return NECWB_SMOKE_FAILURE("load type 4 columns");
    loadType->setCurrentIndex(loadType->findData(5));
    if (loadTable->horizontalHeaderItem(5)->text() != QStringLiteral("Conductivity (MS/m)")
        || loadTable->item(0, 6)->flags().testFlag(Qt::ItemIsEditable))
        return NECWB_SMOKE_FAILURE("load type 2 columns");
    loadType->setCurrentIndex(loadType->findData(4));
    auto validLoadEmitted = false;
    auto emittedLoadType = -1;
    auto emittedLoadSourceLine = std::size_t{1};
    auto emittedLoadFirstSegment = -1;
    auto emittedLoadLastSegment = -1;
    auto emittedLoadInductance = 0.0;
    auto emittedLoadCapacitance = 0.0;
    auto validLineEmitted = false;
    auto emittedLineSourceLine = std::size_t{1};
    auto emittedLineLength = 0.0;
    QObject::connect(&loadNetwork, &necwb::ui::LoadNetworkEditor::loadChanged,
        [&validLoadEmitted, &emittedLoadType, &emittedLoadSourceLine,
            &emittedLoadFirstSegment, &emittedLoadLastSegment,
            &emittedLoadInductance, &emittedLoadCapacitance](const auto& load) {
            validLoadEmitted = true;
            emittedLoadType = load.type;
            emittedLoadSourceLine = load.sourceLine;
            emittedLoadFirstSegment = load.firstSegment;
            emittedLoadLastSegment = load.lastSegment;
            emittedLoadInductance = load.value2;
            emittedLoadCapacitance = load.value3;
        });
    QObject::connect(&loadNetwork, &necwb::ui::LoadNetworkEditor::transmissionLineChanged,
        [&validLineEmitted, &emittedLineSourceLine, &emittedLineLength](const auto& line) {
            validLineEmitted = true;
            emittedLineSourceLine = line.sourceLine;
            emittedLineLength = line.lengthMeters;
        });
    loadTable->item(0, 1)->setText(QStringLiteral("99")); applyLoad->click();
    const auto invalidLoadBlocked = !validLoadEmitted && loadValidation->isVisibleTo(&loadNetwork);
    const auto invalidLoadRemainsPending = loadNetwork.hasPendingEdits()
        && applyLoad->property("pendingChanges").toBool();
    loadTable->item(0, 1)->setText(QStringLiteral("1")); applyLoad->click();
    addLoad->click();
    const auto draftDidNotEmit = emittedLoadType == 4 && loadTable->rowCount() == 2;
    auto* draftType = qobject_cast<QComboBox*>(loadTable->cellWidget(1, 0));
    auto* draftScope = qobject_cast<QComboBox*>(loadTable->cellWidget(1, 2));
    if (draftType == nullptr || draftScope == nullptr)
        return NECWB_SMOKE_FAILURE("draft load controls");
    draftType->setCurrentIndex(draftType->findData(2));
    draftScope->setCurrentIndex(draftScope->findData(true));
    loadTable->item(1, 6)->setText(QStringLiteral("4.700"));
    loadTable->item(1, 7)->setText(QStringLiteral("22.000"));
    applyLoad->click();
    const auto selectedDraftTypeEmitted = emittedLoadType == 2 && emittedLoadSourceLine == 0
        && emittedLoadFirstSegment == 0 && emittedLoadLastSegment == 0
        && std::abs(emittedLoadInductance - 4.7e-6) < 1.0e-15
        && std::abs(emittedLoadCapacitance - 22.0e-12) < 1.0e-20;
    lineTable->selectRow(0); lineTable->item(0, 4)->setText(QStringLiteral("0")); applyLine->click();
    const auto invalidLineBlocked = !validLineEmitted;
    const auto invalidLineRemainsPending = loadNetwork.hasPendingEdits()
        && applyLine->property("pendingChanges").toBool();
    lineTable->item(0, 4)->setText(QStringLiteral("75")); applyLine->click();
    const auto lineUnitConverted = std::abs(emittedLineLength - 3.048) < 1.0e-12;
    addLine->click();
    const auto lineDraftDidNotEmit = emittedLineSourceLine == 8 && lineTable->rowCount() == 2;
    auto* draftWire1 = qobject_cast<QComboBox*>(lineTable->cellWidget(1, 0));
    auto* draftSegment1 = qobject_cast<QComboBox*>(lineTable->cellWidget(1, 1));
    auto* draftWire2 = qobject_cast<QComboBox*>(lineTable->cellWidget(1, 2));
    auto* draftSegment2 = qobject_cast<QComboBox*>(lineTable->cellWidget(1, 3));
    if (draftWire1 == nullptr || draftSegment1 == nullptr
        || draftWire2 == nullptr || draftSegment2 == nullptr)
        return NECWB_SMOKE_FAILURE("draft transmission-line controls");
    applyLine->click();
    const auto lineDraftApplied = emittedLineSourceLine == 0
        && draftWire1->currentData().toInt() == 1
        && draftSegment1->currentData().toInt() == 1
        && draftWire2->currentData().toInt() == 2
        && draftSegment2->currentData().toInt() == 1;
    QTemporaryDir directory;
    necwb::ui::AnalysisRunStore store(directory.path());
    auto run = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"));
    run.status = QStringLiteral("Completed");
    run.durationSeconds = 1.25;
    run.outputBytes = 8192;
    run.frequencyCount = 3;
    run.hasImpedance = true;
    run.hasCurrents = true;
    run.hasRadiation = true;
    if (!store.save(run)) {
        return NECWB_SMOKE_FAILURE("analysis run storage");
    }
    const QByteArray reviewOutput =
        " FREQUENCY : 1.4100E+01 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 6 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n";
    QFile reviewOutputFile(QDir(run.directory).filePath(QStringLiteral("model.out")));
    QFile reviewSourceFile(QDir(run.directory).filePath(QStringLiteral("model.source.nec")));
    QFile reviewDeckFile(QDir(run.directory).filePath(QStringLiteral("model.nec")));
    const auto reviewArtifactsWritten = reviewOutputFile.open(QIODevice::WriteOnly)
        && reviewOutputFile.write(reviewOutput) == reviewOutput.size()
        && reviewSourceFile.open(QIODevice::WriteOnly)
        && reviewSourceFile.write("CM authored source\nGW 1 11 0 0 0 1 0 0 0.001\nGE 0\nEN\n") > 0
        && reviewDeckFile.open(QIODevice::WriteOnly)
        && reviewDeckFile.write("CM generated deck\nGW 1 11 0 0 0 1 0 0 0.001\nGE 0\nEN\n") > 0;
    reviewOutputFile.close();
    reviewSourceFile.close();
    reviewDeckFile.close();
    necwb::ui::RunReviewWindow runReview;
    const auto reviewLoaded = reviewArtifactsWritten
        && runReview.showRun(run, QStringLiteral("active-model.nec"));
    application.processEvents();
    auto* reviewTabs = runReview.findChild<QTabWidget*>(QStringLiteral("runReviewTabs"));
    auto* reviewIdentity = runReview.findChild<QLabel*>(QStringLiteral("runReviewIdentity"));
    auto* reviewActiveModel = runReview.findChild<QLabel*>(QStringLiteral("runReviewActiveModel"));
    auto* reviewSnapshot = runReview.findChild<QWidget*>(
        QStringLiteral("historicalInputSnapshotPanel"));
    auto* reviewUnusedInputPage = runReview.findChild<QWidget*>(
        QStringLiteral("runReviewInputPage"));
    const auto runReviewValid = reviewLoaded && runReview.isVisible()
        && reviewTabs != nullptr && reviewTabs->count() == 3
        && reviewTabs->tabText(0) == QStringLiteral("Summary")
        && reviewTabs->tabText(1) == QStringLiteral("Impedance")
        && reviewTabs->tabText(2) == QStringLiteral("Raw Output")
        && reviewIdentity != nullptr
        && reviewIdentity->text().contains(QStringLiteral("test-dipole.nec"))
        && reviewActiveModel != nullptr
        && reviewActiveModel->text().contains(QStringLiteral("active-model.nec"))
        && reviewSnapshot != nullptr && !reviewSnapshot->isHidden()
        && reviewUnusedInputPage != nullptr && reviewUnusedInputPage->isHidden();
    runReview.close();
    auto session = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"),
        QStringLiteral("optimization-session"));
    session.status = QStringLiteral("Completed");
    session.summary = QStringLiteral("Best candidate: length = 10");
    session.candidateCount = 2;
    if (!store.save(session)) return NECWB_SMOKE_FAILURE("optimization session storage");
    auto candidate = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"),
        QStringLiteral("optimization-candidate"), session.id);
    candidate.status = QStringLiteral("Completed");
    if (!store.save(candidate)) return NECWB_SMOKE_FAILURE("optimization candidate storage");
    const auto loaded = store.load();
    const auto loadedRun = std::ranges::find(loaded, run.id, &necwb::ui::AnalysisRunRecord::id);
    const auto loadedSession = std::ranges::find(loaded, session.id, &necwb::ui::AnalysisRunRecord::id);
    const auto loadedCandidate = std::ranges::find(loaded, candidate.id, &necwb::ui::AnalysisRunRecord::id);
    const auto storeValid = loaded.size() == 3 && loadedRun != loaded.end()
        && loadedRun->status == QStringLiteral("Completed")
        && loadedRun->sourceFile == QStringLiteral("/tmp/test-dipole.nec")
        && loadedRun->backend == QStringLiteral("nec2")
        && loadedRun->durationSeconds == 1.25 && loadedRun->outputBytes == 8192
        && loadedRun->frequencyCount == 3 && loadedRun->hasImpedance
        && loadedRun->hasCurrents && loadedRun->hasRadiation
        && loadedSession != loaded.end()
        && loadedSession->runType == QStringLiteral("optimization-session")
        && loadedSession->candidateCount == 2
        && loadedCandidate != loaded.end() && loadedCandidate->parentId == session.id;
    const auto runDeletionSafe = !store.remove(directory.path())
        && store.removeGroup(session.id, session.directory)
        && !QFileInfo::exists(session.directory) && !QFileInfo::exists(candidate.directory)
        && store.remove(run.directory) && !QFileInfo::exists(run.directory);
    const auto passed = !image.isNull() && engineeringAxes && candidatePlotValid
        && !summaryImage.isNull()
        && !fieldImage.isNull() && !currentImage.isNull()
        && !dashboardImage.isNull() && dashboardStateVisible && dashboard3DIsOverview
        && resultPanelDetached && resultPanelReattached
        && !welcomeImage.isNull() && welcomeStateValid && !examplesOpened && storeValid
        && runReviewValid
        && runDeletionSafe
        && !structuredImage.isNull() && invalidEditBlocked && duplicateFrequencyBlocked
        && structuredSelectionWorks && structuredHierarchyValid
        && descriptiveDropdown
        && trailingFieldsPreserved
        && arcFieldsLabeled && arcEditCommitted && helixFieldsLabeled
        && helixDefaultValid && generatedCardsExcludedFromOther
        && editedCard == QStringLiteral("EX 0 1 7 0 1 0 2.5")
        && addedCard == QStringLiteral("LD 0 1 1 11 0 0 0") && deletedLine == 3
        && !wireImage.isNull() && wireEditCommitted && gaugeDropdownValid
        && structuredGaugeRangeValid && structuredGaugeCommitted && customRadiusPreserved
        && symbolicCoordinatesLocked
        && symbolicGaugeEnabled
        && setupColumnsValid && groundPresetValuesValid && customGroundDetected
        && groundApplyHighlighted && groundDiscardRestored
        && referenceImpedancePending && referenceImpedanceControlValid
        && parameterEditorValid && newParameterValid
        && multiplePatternEditorValid
        && convergenceHistoryControlsValid
        && segmentationApplyAccepted
        && radiationControlsSynchronized && radiationSweepSelectable
        && nonPatternFrequencyIgnored && radiationFrequencySynchronized
        && mixedPatternCutsSeparated && horizontalDatasetSelectsCompatibleCut
        && horizontalMaxGainCutStaysCompatible && radiationMetricsVisible
        && radiationExportReady && maxGainCutSelected && signedThetaTiesNormalized
        && signedThetaCutStaysOpen
        && missingRadiationReported
        && missingRadiationDisablesDataExport
        && resultOriginPreserved && semanticResultExtent
        && !attachmentImage.isNull() && invalidLoadBlocked && invalidLoadRemainsPending
        && validLoadEmitted
        && draftDidNotEmit && selectedDraftTypeEmitted
        && invalidLineBlocked && invalidLineRemainsPending
        && validLineEmitted && lineUnitConverted
        && lineDraftDidNotEmit && lineDraftApplied;
    if (!passed) qWarning() << "structured smoke state" << invalidEditBlocked << editedCard
        << addedCard << deletedLine << "wire committed" << wireEditCommitted
        << "segmentation apply" << segmentationApplyAccepted
        << "run store" << storeValid << "run deletion" << runDeletionSafe
        << "ground presets" << groundPresetValuesValid << customGroundDetected
        << groundPreset->count() << groundPreset->currentIndex()
        << groundPermittivity->value() << groundConductivity->value()
        << "candidate plot" << candidatePlotValid
        << (candidateScorePlot == nullptr ? QVariant{} : candidateScorePlot->property("pointCount"))
        << (candidateScorePlot == nullptr ? QVariant{} : candidateScorePlot->property("bestX"))
        << (candidateScorePlot == nullptr ? QVariant{} : candidateScorePlot->property("leftAxisMinimum"))
        << (candidateScorePlot == nullptr ? QVariant{} : candidateScorePlot->property("leftAxisMaximum"))
        << "result splitter" << (optimizationResultsSplitter == nullptr
            ? -1 : optimizationResultsSplitter->count());
    return passed ? EXIT_SUCCESS : NECWB_SMOKE_FAILURE("final integrated smoke state");
}
