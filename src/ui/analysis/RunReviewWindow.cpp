#include "ui/analysis/RunReviewWindow.h"

#include "analysis/NecOutputParser.h"
#include "nec/NecModelConverter.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"
#include "ui/analysis/AverageGainResultsView.h"
#include "ui/analysis/ConvergenceWorkspace.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/analysis/ImpedanceResultsView.h"
#include "ui/analysis/ResultsSummaryView.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/optimization/OptimizationWorkspace.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace necwb::ui {
namespace {

auto readTextFile(const QString& path) -> QString
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
}

auto runContext(const AnalysisRunRecord& record) -> QString
{
    const auto modelName = record.sourceFile.isEmpty()
        ? QObject::tr("Archived model.nec") : QFileInfo(record.sourceFile).fileName();
    const auto started = record.started.isValid()
        ? record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : record.id;
    const auto backend = record.backend.isEmpty() ? QObject::tr("Unknown backend") : record.backend;
    const auto purpose = record.runType == QStringLiteral("quick-sweep")
        ? record.summary.isEmpty() ? QObject::tr(" · Quick Sweep")
                                   : QObject::tr(" · Quick Sweep: %1").arg(record.summary)
        : QString{};
    return QObject::tr("Model: %1 · Run: %2%3 · Backend: %4")
        .arg(modelName, started, purpose, backend);
}

auto sameFrequency(double first, double second) -> bool
{
    return std::abs(first - second) <= 1.0e-9 * std::max({1.0, std::abs(first), std::abs(second)});
}

}

RunReviewWindow::RunReviewWindow(QWidget* parent)
    : QDialog(parent, Qt::Window)
{
    setObjectName(QStringLiteral("runReviewWindow"));
    setWindowTitle(tr("Run Review — NEC Workbench"));
    setModal(false);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    auto* layout = new QVBoxLayout(this);
    auto* headingRow = new QHBoxLayout;
    identityLabel_ = new QLabel(this);
    identityLabel_->setObjectName(QStringLiteral("runReviewIdentity"));
    identityLabel_->setWordWrap(true);
    auto identityFont = identityLabel_->font();
    identityFont.setBold(true);
    identityLabel_->setFont(identityFont);
    openSnapshotButton_ = new QPushButton(tr("Open Snapshot as New Model"), this);
    openSnapshotButton_->setObjectName(QStringLiteral("runReviewOpenSnapshot"));
    auto* openFolderButton = new QPushButton(tr("Open Run Folder"), this);
    openFolderButton->setObjectName(QStringLiteral("runReviewOpenFolder"));
    headingRow->addWidget(identityLabel_, 1);
    headingRow->addWidget(openSnapshotButton_);
    headingRow->addWidget(openFolderButton);
    activeModelLabel_ = new QLabel(this);
    activeModelLabel_->setObjectName(QStringLiteral("runReviewActiveModel"));
    statusLabel_ = new QLabel(tr("Select a run to review."), this);
    statusLabel_->setObjectName(QStringLiteral("runReviewStatus"));
    statusLabel_->setWordWrap(true);

    frequencyBar_ = new QWidget(this);
    auto* frequencyLayout = new QHBoxLayout(frequencyBar_);
    frequencyLayout->setContentsMargins(0, 0, 0, 0);
    frequencyLayout->addWidget(new QLabel(tr("Result Frequency:"), frequencyBar_));
    frequencyControl_ = new QComboBox(frequencyBar_);
    frequencyControl_->setObjectName(QStringLiteral("runReviewFrequency"));
    frequencyControl_->setMinimumContentsLength(18);
    frequencyLayout->addWidget(frequencyControl_);
    frequencyLayout->addStretch();

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("runReviewTabs"));
    tabs_->setDocumentMode(true);
    summaryView_ = new ResultsSummaryView(tabs_);
    impedancePage_ = new QTabWidget(tabs_);
    impedancePage_->setDocumentMode(true);
    impedanceView_ = new ImpedanceResultsView(impedancePage_);
    sweepPlotsView_ = new SweepPlotsView(impedancePage_);
    impedancePage_->addTab(impedanceView_, tr("Table"));
    impedancePage_->addTab(sweepPlotsView_, tr("Plots"));
    currentsView_ = new CurrentDistributionView(tabs_);
    radiationPage_ = new QTabWidget(tabs_);
    radiationPage_->setDocumentMode(true);
    radiationPatternView_ = new RadiationPatternView(radiationPage_);
    radiation3DView_ = new Radiation3DView(radiationPage_);
    radiationPage_->addTab(radiationPatternView_, tr("2D Pattern"));
    radiationPage_->addTab(radiation3DView_, tr("3D Pattern"));
    radiationPerformanceView_ = new RadiationPerformanceView(radiationPage_);
    radiationPage_->addTab(radiationPerformanceView_, tr("Performance vs Frequency"));
    radiationPatternView_->setSettingsChangedCallback(
        [this](const analysis::RadiationDisplaySettings& settings) {
            radiation3DView_->setDisplaySettings(settings);
            radiationPerformanceView_->setComponent(settings.component);
        });
    radiation3DView_->setSettingsChangedCallback(
        [this](const analysis::RadiationDisplaySettings& settings) {
            radiationPatternView_->setDisplaySettings(settings);
            radiationPerformanceView_->setComponent(settings.component);
        });
    radiationPerformanceView_->setComponentChangedCallback(
        [this](analysis::RadiationComponent component) {
            radiationPatternView_->setComponent(component);
            radiation3DView_->setComponent(component);
        });
    rawOutput_ = new QPlainTextEdit(tabs_);
    rawOutput_->setObjectName(QStringLiteral("runReviewRawOutput"));
    rawOutput_->setReadOnly(true);
    rawOutput_->setLineWrapMode(QPlainTextEdit::NoWrap);
    rawOutput_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    inputPage_ = new QWidget(tabs_);
    inputPage_->setObjectName(QStringLiteral("runReviewInputPage"));
    inputPage_->hide();
    auto* inputLayout = new QVBoxLayout(inputPage_);
    inputSource_ = new QComboBox(inputPage_);
    inputSource_->setObjectName(QStringLiteral("runReviewInputSource"));
    inputText_ = new QPlainTextEdit(inputPage_);
    inputText_->setObjectName(QStringLiteral("runReviewInputText"));
    inputText_->setReadOnly(true);
    inputText_->setLineWrapMode(QPlainTextEdit::NoWrap);
    inputText_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    inputLayout->addWidget(inputSource_);
    inputLayout->addWidget(inputText_, 1);

    connect(inputSource_, &QComboBox::currentIndexChanged,
        this, [this] { refreshInputSnapshot(); });
    connect(frequencyControl_, &QComboBox::currentIndexChanged,
        this, [this] { refreshFrequency(); });
    connect(openFolderButton, &QPushButton::clicked, this, [this] {
        if (!record_.directory.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(record_.directory));
    });
    connect(openSnapshotButton_, &QPushButton::clicked, this, [this] {
        if (openSnapshotCallback_)
            openSnapshotCallback_(record_.directory,
                record_.sourceFile.isEmpty() ? tr("Archived model.nec")
                                             : QFileInfo(record_.sourceFile).fileName(),
                runContext(record_));
    });

    layout->addLayout(headingRow);
    layout->addWidget(activeModelLabel_);
    layout->addWidget(statusLabel_);
    layout->addWidget(frequencyBar_);
    layout->addWidget(tabs_, 1);
    const auto geometry = QSettings{}.value(QStringLiteral("runReviewWindow/geometry")).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    else resize(1050, 760);
}

void RunReviewWindow::setOpenSnapshotCallback(
    std::function<void(const QString&, const QString&, const QString&)> callback)
{
    openSnapshotCallback_ = std::move(callback);
}

auto RunReviewWindow::showRun(const AnalysisRunRecord& record,
    const QString& activeModelName) -> bool
{
    record_ = record;
    const auto modelName = record.sourceFile.isEmpty()
        ? tr("Archived model.nec") : QFileInfo(record.sourceFile).fileName();
    const auto started = record.started.isValid()
        ? record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : record.id;
    identityLabel_->setText(record.runType == QStringLiteral("quick-sweep")
        ? tr("Historical Quick Sweep — %1 — %2 — %3")
            .arg(modelName, started,
                record.backend.isEmpty() ? tr("Unknown backend") : record.backend)
        : tr("Historical Run — %1 — %2 — %3")
            .arg(modelName, started,
                record.backend.isEmpty() ? tr("Unknown backend") : record.backend));
    activeModelLabel_->setText(tr("Active Model: %1 · This archived review does not modify it.")
        .arg(activeModelName.isEmpty() ? tr("None") : activeModelName));
    openSnapshotButton_->setEnabled(QFileInfo::exists(
        QDir(record.directory).filePath(QStringLiteral("model.nec"))));
    tabs_->clear();
    frequencyBar_->hide();

    bool loaded = false;
    if (record.runType == QStringLiteral("optimization-session"))
        loaded = showOptimizationSession(record);
    else if (record.runType == QStringLiteral("convergence-session"))
        loaded = showConvergenceSession(record);
    else if (record.runType == QStringLiteral("average-gain-test"))
        loaded = showAverageGainRun(record, runContext(record));
    else
        loaded = showAnalysisRun(record, runContext(record));
    present();
    return loaded;
}

auto RunReviewWindow::showAnalysisRun(const AnalysisRunRecord& record,
    const QString& context) -> bool
{
    inputPage_->hide();
    const auto outputText = readTextFile(QDir(record.directory).filePath(QStringLiteral("model.out")));
    authoredSource_ = readTextFile(QDir(record.directory).filePath(QStringLiteral("model.source.nec")));
    generatedDeck_ = readTextFile(QDir(record.directory).filePath(QStringLiteral("model.nec")));
    rawOutput_->setPlainText(outputText.isEmpty() ? tr("No archived solver output is available.") : outputText);
    auto result = analysis::NecOutputParser{}.parse(outputText.toStdString());
    if (!authoredSource_.isEmpty()) {
        const auto sourceDocument = nec::NecParser{}.parse(authoredSource_.toStdString());
        result.referenceImpedanceOhms = model::referenceImpedanceOhms(
            nec::NecSetupConverter{}.convert(sourceDocument));
    }
    model::AntennaModel archivedModel;
    if (!generatedDeck_.isEmpty()) {
        const auto document = nec::NecParser{}.parse(generatedDeck_.toStdString());
        archivedModel = nec::NecModelConverter{}.convert(document).model;
    }
    summaryView_->setResults(result, context);
    summaryView_->setHistoricalInputSnapshot(authoredSource_, generatedDeck_);
    impedanceView_->setResults(result, context);
    sweepPlotsView_->setResults(result, context);
    currentsView_->setModel(archivedModel);
    currentsView_->setResults(result, context);
    radiationPatternView_->setResults(result, context);
    radiation3DView_->setModel(archivedModel);
    radiation3DView_->setResults(result, context);
    radiationPerformanceView_->setResults(result, context);
    tabs_->addTab(summaryView_, tr("Summary"));
    if (!result.feedpoints.empty()) tabs_->addTab(impedancePage_, tr("Impedance"));
    if (!result.currents.empty()) tabs_->addTab(currentsView_, tr("Currents"));
    if (!result.radiation.empty()) tabs_->addTab(radiationPage_, tr("Radiation"));
    tabs_->addTab(rawOutput_, tr("Raw Output"));

    std::vector<double> frequencies;
    const auto addFrequency = [&frequencies](double frequency) {
        if (std::ranges::none_of(frequencies, [frequency](double existing) {
                return sameFrequency(existing, frequency);
            })) frequencies.push_back(frequency);
    };
    for (const auto& value : result.feedpoints) addFrequency(value.frequencyMHz);
    for (const auto& value : result.currents) addFrequency(value.frequencyMHz);
    for (const auto& value : result.radiation) addFrequency(value.frequencyMHz);
    std::ranges::sort(frequencies);
    frequencyControl_->clear();
    for (const auto frequency : frequencies)
        frequencyControl_->addItem(tr("%1 MHz").arg(frequency, 0, 'f', 3), frequency);
    frequencyBar_->setVisible(!frequencies.empty());
    if (!frequencies.empty()) refreshFrequency();
    const auto loaded = !result.feedpoints.empty() || !result.currents.empty()
        || !result.radiation.empty();
    statusLabel_->setText(loaded
        ? tr("Archived analysis results · read-only")
        : tr("No supported structured result data was found; raw output remains available."));
    return loaded;
}

auto RunReviewWindow::showAverageGainRun(const AnalysisRunRecord& record,
    const QString& context) -> bool
{
    if (averageGainView_ == nullptr) {
        averageGainView_ = new AverageGainResultsView(tabs_);
        if (auto* button = averageGainView_->findChild<QToolButton*>(
                QStringLiteral("runAverageGainFromValidationButton"))) button->hide();
    }
    const auto outputText = readTextFile(QDir(record.directory).filePath(QStringLiteral("model.out")));
    authoredSource_ = readTextFile(QDir(record.directory).filePath(QStringLiteral("model.source.nec")));
    generatedDeck_ = readTextFile(QDir(record.directory).filePath(QStringLiteral("model.nec")));
    rawOutput_->setPlainText(outputText.isEmpty() ? tr("No archived solver output is available.") : outputText);
    setInputSnapshot(authoredSource_, generatedDeck_);
    auto frequencyMHz = 0.0;
    auto environment = analysis::AverageGainEnvironment::FreeSpace;
    QFile metadataFile(QDir(record.directory).filePath(QStringLiteral("agt.json")));
    if (metadataFile.open(QIODevice::ReadOnly)) {
        const auto metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
        frequencyMHz = metadata.value(QStringLiteral("frequencyMHz")).toDouble();
        if (metadata.value(QStringLiteral("environment")).toString() == QStringLiteral("perfect-ground"))
            environment = analysis::AverageGainEnvironment::PerfectGround;
    }
    const auto result = analysis::NecOutputParser{}.parse(outputText.toStdString());
    if (result.averagePowerGain) {
        const auto expected = environment == analysis::AverageGainEnvironment::PerfectGround ? 2.0 : 1.0;
        averageGainView_->setResult(analysis::assessAverageGain(*result.averagePowerGain, expected),
            frequencyMHz, environment, result.averagingSolidAnglePi, context);
        statusLabel_->setText(tr("Archived Average Gain Test · read-only"));
    } else {
        averageGainView_->setFailure(tr("The archived output contains no Average Power Gain value."), context);
        statusLabel_->setText(tr("Archived Average Gain Test is incomplete."));
    }
    tabs_->addTab(averageGainView_, tr("Average Gain Test"));
    tabs_->addTab(rawOutput_, tr("Raw Output"));
    tabs_->addTab(inputPage_, tr("Input Snapshot"));
    return result.averagePowerGain.has_value();
}

auto RunReviewWindow::showOptimizationSession(const AnalysisRunRecord& record) -> bool
{
    if (optimizationView_ == nullptr) {
        optimizationView_ = new OptimizationWorkspace(tabs_);
        optimizationView_->setReturnToCurrentWorkCallback([this] { close(); });
    }
    const auto loaded = optimizationView_->loadSession(record.id);
    if (loaded) tabs_->addTab(optimizationView_, tr("Optimization"));
    statusLabel_->setText(loaded ? tr("Archived optimization session · read-only")
                                 : tr("The archived optimization session could not be loaded."));
    return loaded;
}

auto RunReviewWindow::showConvergenceSession(const AnalysisRunRecord& record) -> bool
{
    if (convergenceView_ == nullptr) {
        convergenceView_ = new ConvergenceWorkspace(tabs_);
        convergenceView_->setReturnToCurrentWorkCallback([this] { close(); });
    }
    const auto loaded = convergenceView_->loadSession(record.id);
    if (loaded) tabs_->addTab(convergenceView_, tr("Segmentation Convergence"));
    statusLabel_->setText(loaded ? tr("Archived convergence session · read-only")
                                 : tr("The archived convergence session could not be loaded."));
    return loaded;
}

void RunReviewWindow::setInputSnapshot(const QString& authoredSource,
    const QString& generatedDeck)
{
    authoredSource_ = authoredSource;
    generatedDeck_ = generatedDeck;
    inputSource_->clear();
    if (!authoredSource_.isEmpty())
        inputSource_->addItem(tr("Authored Source"), QStringLiteral("authored"));
    if (!generatedDeck_.isEmpty())
        inputSource_->addItem(tr("Generated Solver Deck"), QStringLiteral("generated"));
    if (inputSource_->count() == 0)
        inputSource_->addItem(tr("Input Snapshot Unavailable"), QStringLiteral("missing"));
    refreshInputSnapshot();
}

void RunReviewWindow::refreshInputSnapshot()
{
    const auto source = inputSource_->currentData().toString();
    inputText_->setPlainText(source == QStringLiteral("authored") ? authoredSource_
        : source == QStringLiteral("generated") ? generatedDeck_
        : tr("No archived input deck is available for this run."));
}

void RunReviewWindow::refreshFrequency()
{
    if (frequencyControl_->currentIndex() < 0) return;
    const auto frequency = frequencyControl_->currentData().toDouble();
    summaryView_->setSelectedFrequency(frequency);
    impedanceView_->setSelectedFrequency(frequency);
    sweepPlotsView_->setSelectedFrequency(frequency);
    currentsView_->setSelectedFrequency(frequency);
    radiationPatternView_->setSelectedFrequency(frequency);
    radiation3DView_->setSelectedFrequency(frequency);
    radiationPerformanceView_->setSelectedFrequency(frequency);
}

void RunReviewWindow::present()
{
    show();
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}

void RunReviewWindow::closeEvent(QCloseEvent* event)
{
    QSettings{}.setValue(QStringLiteral("runReviewWindow/geometry"), saveGeometry());
    QDialog::closeEvent(event);
}

}
