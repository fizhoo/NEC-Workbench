#include "analysis/AnalysisResult.h"
#include "ui/DisplayFormat.h"
#include "ui/DetachablePanel.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/ResultsSummaryView.h"
#include "ui/analysis/AverageGainResultsView.h"
#include "ui/analysis/AnalysisRequestEditor.h"
#include "ui/analysis/ConvergenceWorkspace.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/dashboard/DashboardPage.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/cards/WireCardEditor.h"
#include "ui/geometry/WirePropertiesDialog.h"
#include "ui/geometry/AutoSegmentationDialog.h"
#include "ui/geometry/GeometryView.h"
#include "ui/geometry/Geometry3DView.h"
#include "ui/optimization/OptimizationWorkspace.h"
#include "ui/optimization/CandidateEvaluator.h"
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
#include <QCoreApplication>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextDocument>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QPushButton>
#include <QShortcut>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <vector>

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
    QApplication application(argc, argv);
    if (necwb::ui::formatDecimal(1.2) != QStringLiteral("1.200")
        || necwb::ui::formatDecimal(0.0004) != QStringLiteral("4.000e-04")) {
        return EXIT_FAILURE;
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
    const auto previousImpedanceScale = plotSettings.value(
        QStringLiteral("results/impedanceScaleMode"));
    const auto previousSwrScale = plotSettings.value(QStringLiteral("results/swrScaleMode"));
    plotSettings.setValue(QStringLiteral("results/impedanceScaleMode"), 0);
    plotSettings.setValue(QStringLiteral("results/swrScaleMode"), 0);
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
    auto* impedanceScaleControl = view.findChild<QComboBox*>(
        QStringLiteral("impedanceScaleControl"));
    auto* swrScaleControl = view.findChild<QComboBox*>(QStringLiteral("swrScaleControl"));
    const auto wholeNumberAxes = impedanceSweepPlot != nullptr && swrSweepPlot != nullptr
        && impedanceSweepPlot->property("yTickStep").toDouble() >= 1.0
        && swrSweepPlot->property("yTickStep").toDouble() >= 1.0
        && std::floor(impedanceSweepPlot->property("yTickStep").toDouble())
            == impedanceSweepPlot->property("yTickStep").toDouble()
        && std::floor(swrSweepPlot->property("yTickStep").toDouble())
            == swrSweepPlot->property("yTickStep").toDouble()
        && impedanceSweepPlot->property("plotLeftMargin").toDouble() >= 100.0
        && swrSweepPlot->property("plotLeftMargin").toDouble() >= 100.0;
    if (impedanceScaleControl == nullptr || swrScaleControl == nullptr
        || impedanceScaleControl->count() != 2 || swrScaleControl->count() != 4) {
        return EXIT_FAILURE;
    }
    impedanceScaleControl->setCurrentIndex(1);
    swrScaleControl->setCurrentIndex(2);
    QPainter alternateScalePainter(&image);
    view.render(&alternateScalePainter);
    alternateScalePainter.end();
    const auto alternateScalesWork = impedanceSweepPlot->property("scaleMode").toInt() == 1
        && swrSweepPlot->property("scaleMode").toInt() == 2
        && swrSweepPlot->property("yMinimum").toDouble() == 1.0
        && swrSweepPlot->property("yMaximum").toDouble() == 3.0;
    if (previousImpedanceScale.isValid()) {
        plotSettings.setValue(QStringLiteral("results/impedanceScaleMode"), previousImpedanceScale);
    } else {
        plotSettings.remove(QStringLiteral("results/impedanceScaleMode"));
    }
    if (previousSwrScale.isValid()) {
        plotSettings.setValue(QStringLiteral("results/swrScaleMode"), previousSwrScale);
    } else {
        plotSettings.remove(QStringLiteral("results/swrScaleMode"));
    }
    if (!alternateScalesWork) return EXIT_FAILURE;
    necwb::ui::ResultsSummaryView resultsSummary;
    resultsSummary.resize(700, 420);
    resultsSummary.setResults(result, QStringLiteral("test-run"));
    resultsSummary.setSelectedFrequency(14.1);
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
        return EXIT_FAILURE;
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
    if (!convergenceHistoryControlsValid) return EXIT_FAILURE;
    QImage convergenceImage(convergence.size(), QImage::Format_ARGB32_Premultiplied);
    convergenceImage.fill(Qt::transparent);
    QPainter convergencePainter(&convergenceImage);
    convergence.render(&convergencePainter);
    convergencePainter.end();
    necwb::ui::OptimizationWorkspace optimization;
    optimization.resize(1000, 700);
    optimization.show();
    application.processEvents();
    optimization.setContext(
        QStringLiteral("SY LONG_FT=95\n"
                       "SY FT=0.3048\n"
                       "SY HALF=LONG_FT*FT/2\n"
                       "GW 1 21 -HALF 0 10 HALF 0 10 0.001\n"
                       "GE 0\nEX 0 1 11 0 1 0\nFR 0 1 0 0 7.15 0\nEN\n"),
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    application.processEvents();
    auto* optimizationConfiguration = optimization.findChild<QSplitter*>(
        QStringLiteral("optimizationConfigurationSplitter"));
    auto* optimizationWorkspaceSplitter = optimization.findChild<QSplitter*>(
        QStringLiteral("optimizationWorkspaceSplitter"));
    auto* optimizationResultsPanel = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationResultsPanel"));
    auto* optimizationResultsScrollArea = optimization.findChild<QScrollArea*>(
        QStringLiteral("optimizationResultsScrollArea"));
    auto* optimizationResultsTable = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationResultsTable"));
    auto* optimizationCandidateDetailsWindow = optimization.findChild<QDialog*>(
        QStringLiteral("optimizationCandidateDetailsWindow"));
    auto* optimizationCandidateDetails = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationCandidateDetails"));
    auto* optimizationVariables = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationVariablesTable"));
    auto* optimizationSweepSettings = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationSweepSettingsTable"));
    auto* optimizationVariableControl = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationVariableControl"));
    auto* optimizationVariablesHeading = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationVariablesHeading"));
    auto* optimizationSweepHeading = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationSweepHeading"));
    auto* optimizationFrequencyMode = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationFrequencyMode"));
    auto* optimizationFrequencyTable = optimization.findChild<QTableWidget*>(
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
    optimization.resize(760, 560);
    application.processEvents();
    const auto settingsBottom = optimizationSweepSettings == nullptr ? 0
        : optimizationSweepSettings->mapTo(&optimization,
              QPoint(0, optimizationSweepSettings->height())).y();
    const auto frequencyPanelTop = optimizationExplicitFrequencies == nullptr ? 0
        : optimizationExplicitFrequencies->mapTo(&optimization, QPoint{}).y();
    const auto configurationBottom = optimizationConfiguration == nullptr ? 0
        : optimizationConfiguration->mapTo(&optimization,
              QPoint(0, optimizationConfiguration->height())).y();
    const auto resultsTop = optimizationResultsPanel == nullptr ? 0
        : optimizationResultsPanel->mapTo(&optimization, QPoint{}).y();
    const auto frequencyControlsContained = optimizationExplicitFrequencies != nullptr
        && optimizationAddAmateurBand != nullptr
        && optimizationExplicitFrequencies->rect().contains(
            optimizationAddAmateurBand->geometry());
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
    const auto optimizationDecimalControls = optimization.findChildren<QDoubleSpinBox*>();
    const auto optimizerUsesThreeDecimals = std::ranges::all_of(
        optimizationDecimalControls, [](const auto* control) { return control->decimals() == 3; });
    if (optimizationConfiguration == nullptr
        || optimizationConfiguration->orientation() != Qt::Horizontal
        || optimizationConfiguration->count() != 2
        || optimizationWorkspaceSplitter == nullptr
        || optimizationWorkspaceSplitter->orientation() != Qt::Vertical
        || optimizationWorkspaceSplitter->count() != 2
        || optimizationResultsPanel == nullptr
        || optimizationResultsScrollArea == nullptr
        || optimizationResultsScrollArea->verticalScrollBarPolicy() != Qt::ScrollBarAsNeeded
        || optimizationResultsTable == nullptr
        || optimizationCandidateDetailsWindow == nullptr
        || optimizationCandidateDetailsWindow->isVisible()
        || optimizationCandidateDetails == nullptr
        || !optimizationCandidateDetailsWindow->isAncestorOf(
            optimizationCandidateDetails)
        || optimization.findChild<QSplitter*>(
            QStringLiteral("optimizationResultsSplitter")) != nullptr
        || configurationBottom > resultsTop
        || optimizationVariables == nullptr
        || optimizationVariableControl == nullptr
        || optimizationSweepSettings == nullptr
        || optimizationVariablesHeading == nullptr
        || optimizationSweepHeading == nullptr
        || optimizationFrequencyMode == nullptr
        || optimizationFrequencyTable == nullptr
        || optimizationAddAmateurBand == nullptr
        || optimizationClearFrequencies == nullptr
        || optimizationExplicitFrequencies == nullptr
        || optimizationContinuousStart == nullptr
        || optimizationContinuousStop == nullptr
        || optimizationContinuousStep == nullptr
        || optimizationApplyBest == nullptr || optimizationApplyBest->isEnabled()
        || settingsBottom > frequencyPanelTop
        || !frequencyControlsContained
        || !hasDeleteShortcut
        || !hasBackspaceShortcut
        || !continuousSweepValid
        || optimizationFrequencyTable->rowCount() != 7
        || optimizationVariablesHeading->height() != optimizationSweepHeading->height()
        || optimizationVariables->mapTo(&optimization, QPoint{}).y()
            != optimizationSweepSettings->mapTo(&optimization, QPoint{}).y()
        || !optimizerUsesThreeDecimals
        || optimizationSweepSettings->rowCount() != 4
        || optimizationVariables->columnCount() != 4
        || optimizationVariables->rowCount() != 3
        || optimizationVariables->item(0, 1)->text() != QStringLiteral("95")
        || optimizationVariables->item(0, 2)->text() != QStringLiteral("95.000")
        || optimizationVariableControl->count() != 3
        || optimizationVariableControl->findText(QStringLiteral("HALF")) < 0
        || qobject_cast<QComboBox*>(optimizationSweepSettings->cellWidget(0, 1)) == nullptr) {
        return EXIT_FAILURE;
    }
    QTemporaryDir candidateDirectory;
    if (!candidateDirectory.isValid()) return EXIT_FAILURE;
    necwb::ui::CandidateEvaluator candidateEvaluator;
    necwb::ui::CandidateEvaluationResult candidateEvaluation;
    auto candidateFinished = false;
    QEventLoop candidateLoop;
    QObject::connect(&candidateEvaluator, &necwb::ui::CandidateEvaluator::finished,
        [&candidateEvaluation, &candidateFinished, &candidateLoop](auto result) {
            candidateEvaluation = std::move(result);
            candidateFinished = true;
            candidateLoop.quit();
        });
    candidateEvaluator.start({
        .authoredSource = QStringLiteral(
            "SY HALF=5\nGW 1 21 -HALF 0 6 HALF 0 6 0.001\nGE 0\n"
            "EX 0 1 11 0 1 0\nFR 0 2 0 0 7 0.1\nRP 0 19 37 1000 0 0 5 10\nEN\n"),
        .variableValues = {{"half", 6.0}},
        .frequencyPlan = {necwb::analysis::FrequencyPlanMode::Explicit,
            {7.0, 14.0}, {}},
        .objective = {.kind = necwb::analysis::OptimizationObjectiveKind::MaximumSwr,
            .referenceImpedance = 50.0},
        .backend = QStringLiteral("nec2"),
        .executable = QCoreApplication::applicationFilePath(),
        .directory = candidateDirectory.path(),
        .timeoutSeconds = 2,
    });
    QTimer::singleShot(2000, &candidateLoop, &QEventLoop::quit);
    candidateLoop.exec();
    QFile candidateDeck(QDir(candidateDirectory.path()).filePath(QStringLiteral("model.nec")));
    const auto candidateDeckText = candidateDeck.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(candidateDeck.readAll()) : QString{};
    QFile candidateSource(QDir(candidateDirectory.path()).filePath(
        QStringLiteral("model.source.nec")));
    const auto candidateSourceText = candidateSource.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(candidateSource.readAll()) : QString{};
    const auto candidateEvaluatorValid = candidateFinished
        && candidateEvaluation.status == necwb::ui::CandidateEvaluationStatus::Completed
        && candidateEvaluation.frequencyCount == 2
        && candidateEvaluation.analysis.feedpoints.size() == 2
        && candidateEvaluation.objective
        && candidateEvaluation.objective->feedpoint
        && candidateEvaluation.objective->feedpoint->frequencyMHz == 14.0
        && candidateDeckText.contains(QStringLiteral("GW 1 21 -6 0 6 6 0 6 0.001"))
        && candidateDeckText.contains(QStringLiteral("FR 0 1 0 0 7 0"))
        && candidateDeckText.contains(QStringLiteral("FR 0 1 0 0 14 0"))
        && !candidateDeckText.contains(QStringLiteral("SY "))
        && !candidateDeckText.contains(QStringLiteral("RP "))
        && candidateSourceText.startsWith(QStringLiteral("SY HALF=5"));
    if (!candidateEvaluatorValid) return EXIT_FAILURE;
    necwb::ui::CurrentDistributionView currents;
    necwb::ui::RadiationPatternView radiation2D;
    necwb::ui::Radiation3DView radiation3D;
    for (auto* resultView : std::vector<QWidget*>{&currents, &radiation2D, &radiation3D}) {
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
        return EXIT_FAILURE;
    }
    currents.setModel(currentAntenna);
    currents.setResults(result, QStringLiteral("test-run"));
    currents.setSelectedFrequency(14.1);
    radiation2D.setResults(result, QStringLiteral("test-run"));
    auto* radiationPolarPlot = radiation2D.findChild<QWidget*>(
        QStringLiteral("radiationPolarPlot"));
    std::vector<QPointF> fullCircle;
    for (auto angle = 0; angle < 360; angle += 10) fullCircle.emplace_back(angle, 1.0);
    std::vector<QPointF> partialCircle;
    for (auto angle = 0; angle <= 90; angle += 10) partialCircle.emplace_back(angle, 1.0);
    if (radiationPolarPlot == nullptr
        || !radiationPolarPlot->property("closedPattern").toBool()
        || !necwb::ui::radiationAnglesCoverCircle(fullCircle)
        || necwb::ui::radiationAnglesCoverCircle(partialCircle)) {
        return EXIT_FAILURE;
    }
    necwb::model::AntennaModel antenna;
    antenna.addWire({1, {0.0, 0.0, -0.5}, {0.0, 0.0, 0.5}, 11, 0.001, 1});
    radiation3D.setModel(antenna);
    necwb::model::AntennaModel elevatedAntenna;
    elevatedAntenna.addWire({1, {-5.0, 0.0, 6.1}, {5.0, 0.0, 6.1}, 11, 0.001, 1});
    const auto resultOriginPreserved = std::abs(
        necwb::ui::resultModelExtentFromOrigin(elevatedAntenna) - 6.1) < 1.0e-12;
    radiation3D.setResults(result, QStringLiteral("test-run"));
    radiation2D.setSettingsChangedCallback([&radiation3D](const auto& settings) {
        radiation3D.setDisplaySettings(settings);
    });
    radiation3D.setSettingsChangedCallback([&radiation2D](const auto& settings) {
        radiation2D.setDisplaySettings(settings);
    });
    auto* radiation2DComponent = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DComponent"));
    auto* radiation2DFrequency = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DFrequency"));
    auto* radiation3DComponent = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DComponent"));
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
    auto* radiation2DExportImage = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DExportImage"));
    auto* radiation2DExportData = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DExportData"));
    auto* radiation3DExportImage = radiation3D.findChild<QPushButton*>(QStringLiteral("radiation3DExportImage"));
    auto* radiation3DExportData = radiation3D.findChild<QPushButton*>(QStringLiteral("radiation3DExportData"));
    if (radiation2DComponent == nullptr || radiation2DFrequency == nullptr
        || radiation3DComponent == nullptr || radiation3DFrequency == nullptr
        || radiation2DFloor == nullptr || radiation3DFloor == nullptr
        || radiation2DSummary == nullptr || radiation3DSummary == nullptr
        || radiation2DCutPlane == nullptr || radiation2DOrientation == nullptr
        || radiation2DDataset == nullptr || radiation3DDataset == nullptr
        || radiation2DMaxGainCut == nullptr
        || radiation2DExportImage == nullptr || radiation2DExportData == nullptr
        || radiation3DExportImage == nullptr || radiation3DExportData == nullptr) return EXIT_FAILURE;
    radiation2DComponent->setCurrentIndex(1);
    radiation2D.setSelectedFrequency(14.2);
    radiation3D.setSelectedFrequency(14.2);
    radiation3DFloor->setCurrentIndex(4);
    application.processEvents();
    const auto radiationSweepSelectable = radiation2DFrequency->count() == 2
        && radiation2DFrequency->currentData().toDouble() == 14.2
        && radiation3DFrequency->currentData().toDouble() == 14.2;
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
        && radiation2DFloor->currentData() == radiation3DFloor->currentData();
    const auto radiationMetricsVisible = radiation2DSummary->text().contains(QStringLiteral("HPBW"))
        && radiation3DSummary->text().contains(QStringLiteral("peak"));
    const auto radiationExportReady = radiation2DExportImage->isEnabled()
        && radiation2DExportData->isEnabled() && radiation3DExportImage->isEnabled()
        && radiation3DExportData->isEnabled();
    radiation2DOrientation->click();
    radiation2DMaxGainCut->click();
    application.processEvents();
    const auto maxGainCutSelected = radiation2DOrientation->text() == QStringLiteral("Vertical Cut")
        && radiation2DCutPlane->currentData().toDouble() == 60.0
        && radiation2DSummary->text().contains(QStringLiteral("20.000 dBi"));
    radiation2D.setSelectedFrequency(14.0);
    application.processEvents();
    const auto missingRadiationReported = radiation2DSummary->text().contains(QStringLiteral("14.000 MHz"))
        && radiation2DSummary->text().contains(QStringLiteral("No"))
        && !radiation2DExportData->isEnabled();
    radiation2D.setSelectedFrequency(14.2);
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
        return EXIT_FAILURE;
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
    if (popOutButton == nullptr) return EXIT_FAILURE;
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
    if (recentList == nullptr || openRecent == nullptr || clearRecent == nullptr) return EXIT_FAILURE;
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
        || !selectStructuredFamily(structuredFamilies, QStringLiteral("EX"))) return EXIT_FAILURE;
    application.processEvents();
    const auto structuredHierarchyValid = structuredFamilies->topLevelItemCount() == 6
        && structuredFamilies->topLevelItem(0)->text(0) == QStringLiteral("Geometry")
        && structuredFamilies->topLevelItem(3)->text(0) == QStringLiteral("Loads & Networks")
        && structuredFamilies->topLevelItem(4)->childCount() == 3;
    if (structuredTable->rowCount() != 1 || !structuredAdd->isEnabled()) return EXIT_FAILURE;
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
    if (wireTable == nullptr || wireTable->rowCount() != 1) return EXIT_FAILURE;
    auto* structuredGauge = qobject_cast<QComboBox*>(wireTable->cellWidget(0, 9));
    if (structuredGauge == nullptr) return EXIT_FAILURE;
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
    symbolicWireEditor.setSymbolicGeometryLines({1});
    symbolicWireEditor.setModel(wireModel);
    auto* symbolicWireTable = symbolicWireEditor.findChild<QTableWidget*>(
        QStringLiteral("wireCardTable"));
    auto* symbolicGauge = symbolicWireTable == nullptr ? nullptr
        : qobject_cast<QComboBox*>(symbolicWireTable->cellWidget(0, 9));
    const auto symbolicGaugeDisabled = symbolicGauge != nullptr && !symbolicGauge->isEnabled();
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
    necwb::ui::ParameterEditor parameterEditor;
    parameterEditor.setResolution(necwb::nec::NecSymbolResolver{}.resolve(
        "SY LENGTH=10, HALF=LENGTH/2\nGW 1 11 -HALF 0 0 HALF 0 0 .001\nGE 0\nEN\n"));
    parameterEditor.resize(760, 520);
    parameterEditor.show();
    application.processEvents();
    auto* parameterTable = parameterEditor.findChild<QTableWidget*>(QStringLiteral("parameterTable"));
    auto* parameterName = parameterEditor.findChild<QLineEdit*>(QStringLiteral("parameterName"));
    auto* parameterExpression = parameterEditor.findChild<QLineEdit*>(QStringLiteral("parameterExpression"));
    auto* updateParameter = parameterEditor.findChild<QPushButton*>(
        QStringLiteral("updateParameterButton"));
    auto parameterSignalValid = false;
    QObject::connect(&parameterEditor, &necwb::ui::ParameterEditor::parameterChanged,
        [&parameterSignalValid](std::size_t sourceLine, const QString& originalName,
            const QString& name, const QString& expression) {
            parameterSignalValid = sourceLine == 1 && originalName == QStringLiteral("HALF")
                && name == QStringLiteral("HALF") && expression == QStringLiteral("LENGTH/2+1");
        });
    if (parameterTable == nullptr || parameterName == nullptr
        || parameterExpression == nullptr || updateParameter == nullptr) return EXIT_FAILURE;
    parameterTable->selectRow(1);
    application.processEvents();
    parameterExpression->setText(QStringLiteral("LENGTH/2+1"));
    updateParameter->click();
    const auto parameterEditorValid = parameterTable->columnCount() == 3
        && parameterTable->rowCount() == 2
        && parameterTable->item(0, 0)->text() == QStringLiteral("LENGTH")
        && parameterTable->item(1, 2)->text() == QStringLiteral("5.000")
        && parameterName->text() == QStringLiteral("HALF")
        && parameterSignalValid;
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
    auto* selectedPatternGroup = requestEditor.findChild<QGroupBox*>(
        QStringLiteral("selectedRadiationPatternGroup"));
    auto* thetaGroup = requestEditor.findChild<QGroupBox*>(QStringLiteral("radiationThetaGroup"));
    auto* phiGroup = requestEditor.findChild<QGroupBox*>(QStringLiteral("radiationPhiGroup"));
    if (requestTable == nullptr || addPattern == nullptr || applyPattern == nullptr
        || patternType == nullptr || resetPattern == nullptr || thetaStart == nullptr
        || thetaEnd == nullptr || phiEnd == nullptr || frequencyPolicyGroup == nullptr
        || selectedPatternGroup == nullptr || thetaGroup == nullptr || phiGroup == nullptr)
        return EXIT_FAILURE;
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
    const auto multiplePatternEditorValid = requestTable->rowCount() == 3
        && requestTable->item(0, 0)->text() == QStringLiteral("Full 3D pattern")
        && requestTable->item(1, 0)->text() == QStringLiteral("Horizontal cut")
        && patternRequestLayoutValid
        && customizedHorizontalRemainsHorizontal && horizontalPresetRestored
        && unclassifiedGridBecomesCustom && fullGridPresetRestored
        && emittedPattern.sourceLine == 0 && emittedPattern.thetaCount == 1
        && emittedPattern.phiCount == 360 && emittedPattern.phiStep == 1.0;
    necwb::model::AntennaModel attachmentModel;
    attachmentModel.addWire({1, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 11, 0.001, 1});
    attachmentModel.addWire({2, {-1.0, 1.0, 0.0}, {1.0, 1.0, 0.0}, 11, 0.001, 2});
    necwb::ui::AutoSegmentationDialog segmentationDialog(attachmentModel, {}, 30.0);
    auto* applySegmentation = segmentationDialog.findChild<QPushButton*>(
        QStringLiteral("applySegmentationButton"));
    if (applySegmentation == nullptr) return EXIT_FAILURE;
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
        || loadValidation == nullptr || loadType == nullptr) return EXIT_FAILURE;
    if (loadTable->item(0, 5)->text() != QStringLiteral("50.000")
        || lineTable->item(0, 4)->text() != QStringLiteral("50.000")
        || lineTable->item(0, 5)->text() != QStringLiteral("10.000")
        || lineTable->horizontalHeaderItem(5)->text() != QStringLiteral("Length (ft)")) return EXIT_FAILURE;
    loadTable->selectRow(0);
    if (loadType->count() != 6 || loadType->currentData().toInt() != 4
        || loadTable->horizontalHeaderItem(5)->text() != QStringLiteral("Resistance (Ω)")
        || loadTable->horizontalHeaderItem(6)->text() != QStringLiteral("Reactance (Ω)")
        || loadTable->item(0, 7)->flags().testFlag(Qt::ItemIsEditable)) return EXIT_FAILURE;
    loadType->setCurrentIndex(loadType->findData(5));
    if (loadTable->horizontalHeaderItem(5)->text() != QStringLiteral("Conductivity (MS/m)")
        || loadTable->item(0, 6)->flags().testFlag(Qt::ItemIsEditable)) return EXIT_FAILURE;
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
    loadTable->item(0, 1)->setText(QStringLiteral("1")); applyLoad->click();
    addLoad->click();
    const auto draftDidNotEmit = emittedLoadType == 4 && loadTable->rowCount() == 2;
    auto* draftType = qobject_cast<QComboBox*>(loadTable->cellWidget(1, 0));
    auto* draftScope = qobject_cast<QComboBox*>(loadTable->cellWidget(1, 2));
    if (draftType == nullptr || draftScope == nullptr) return EXIT_FAILURE;
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
    lineTable->item(0, 4)->setText(QStringLiteral("75")); applyLine->click();
    const auto lineUnitConverted = std::abs(emittedLineLength - 3.048) < 1.0e-12;
    addLine->click();
    const auto lineDraftDidNotEmit = emittedLineSourceLine == 8 && lineTable->rowCount() == 2;
    auto* draftWire1 = qobject_cast<QComboBox*>(lineTable->cellWidget(1, 0));
    auto* draftSegment1 = qobject_cast<QComboBox*>(lineTable->cellWidget(1, 1));
    auto* draftWire2 = qobject_cast<QComboBox*>(lineTable->cellWidget(1, 2));
    auto* draftSegment2 = qobject_cast<QComboBox*>(lineTable->cellWidget(1, 3));
    if (draftWire1 == nullptr || draftSegment1 == nullptr
        || draftWire2 == nullptr || draftSegment2 == nullptr) return EXIT_FAILURE;
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
        return EXIT_FAILURE;
    }
    auto session = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"),
        QStringLiteral("optimization-session"));
    session.status = QStringLiteral("Completed");
    session.summary = QStringLiteral("Best candidate: length = 10");
    session.candidateCount = 2;
    if (!store.save(session)) return EXIT_FAILURE;
    auto candidate = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"),
        QStringLiteral("optimization-candidate"), session.id);
    candidate.status = QStringLiteral("Completed");
    if (!store.save(candidate)) return EXIT_FAILURE;
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
    const auto passed = !image.isNull() && wholeNumberAxes && !summaryImage.isNull()
        && !fieldImage.isNull() && !currentImage.isNull()
        && !dashboardImage.isNull() && dashboardStateVisible && dashboard3DIsOverview
        && resultPanelDetached && resultPanelReattached
        && !welcomeImage.isNull() && welcomeStateValid && !examplesOpened && storeValid
        && runDeletionSafe
        && !structuredImage.isNull() && invalidEditBlocked && duplicateFrequencyBlocked
        && structuredSelectionWorks && structuredHierarchyValid
        && descriptiveDropdown
        && trailingFieldsPreserved
        && editedCard == QStringLiteral("EX 0 1 7 0 1 0 2.5")
        && addedCard == QStringLiteral("LD 0 1 1 11 0 0 0") && deletedLine == 3
        && !wireImage.isNull() && wireEditCommitted && gaugeDropdownValid
        && structuredGaugeRangeValid && structuredGaugeCommitted && customRadiusPreserved
        && symbolicGaugeDisabled
        && setupColumnsValid && parameterEditorValid
        && multiplePatternEditorValid
        && convergenceHistoryControlsValid
        && segmentationApplyAccepted
        && radiationControlsSynchronized && radiationSweepSelectable
        && mixedPatternCutsSeparated && horizontalDatasetSelectsCompatibleCut
        && horizontalMaxGainCutStaysCompatible && radiationMetricsVisible
        && radiationExportReady && maxGainCutSelected && missingRadiationReported
        && missingRadiationDisablesDataExport
        && resultOriginPreserved
        && !attachmentImage.isNull() && invalidLoadBlocked && validLoadEmitted
        && draftDidNotEmit && selectedDraftTypeEmitted
        && invalidLineBlocked && validLineEmitted && lineUnitConverted
        && lineDraftDidNotEmit && lineDraftApplied;
    if (!passed) qWarning() << "structured smoke state" << invalidEditBlocked << editedCard
        << addedCard << deletedLine << "wire committed" << wireEditCommitted
        << "segmentation apply" << segmentationApplyAccepted
        << "run store" << storeValid << "run deletion" << runDeletionSafe;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
