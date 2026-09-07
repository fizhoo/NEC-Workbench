#include "ui/analysis/ConvergenceWorkspace.h"

#include "ui/DisplayFormat.h"

#include "analysis/NecOutputParser.h"
#include "analysis/SegmentationConvergence.h"
#include "analysis/SolverCommand.h"
#include "nec/NecSymbolResolver.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"

#include <QAbstractItemView>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <ranges>
#include <utility>

namespace necwb::ui {
namespace {

enum ResultColumn {
    ScaleColumn,
    SegmentsColumn,
    ResistanceColumn,
    ReactanceColumn,
    ImpedanceChangeColumn,
    GainColumn,
    GainChangeColumn,
    AssessmentColumn,
    RunColumn,
    ResultColumnCount
};

auto writeFile(const QString& path, const QByteArray& contents) -> bool
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

auto numericItem(double value) -> QTableWidgetItem*
{
    auto* item = new QTableWidgetItem(formatDecimal(value));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

auto optionalItem(const std::optional<double>& value) -> QTableWidgetItem*
{
    return value ? numericItem(*value) : new QTableWidgetItem(QStringLiteral("—"));
}

auto nearestFeedpoint(const analysis::AnalysisResult& result, double frequencyMHz)
    -> const analysis::FeedpointResult*
{
    if (result.feedpoints.empty()) return nullptr;
    return &*std::ranges::min_element(result.feedpoints, {}, [frequencyMHz](const auto& value) {
        return std::abs(value.frequencyMHz - frequencyMHz);
    });
}

auto peakGain(const analysis::AnalysisResult& result, double frequencyMHz) -> std::optional<double>
{
    std::optional<double> peak;
    for (const auto& sample : result.radiation) {
        if (std::abs(sample.frequencyMHz - frequencyMHz)
                > 1.0e-9 * std::max(1.0, std::abs(frequencyMHz))) continue;
        if (!peak || sample.totalGainDb > *peak) peak = sample.totalGainDb;
    }
    return peak;
}

}

ConvergenceWorkspace::ConvergenceWorkspace(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    historicalBanner_ = new QFrame(this);
    historicalBanner_->setObjectName(QStringLiteral("convergenceHistoricalBanner"));
    static_cast<QFrame*>(historicalBanner_)->setFrameShape(QFrame::StyledPanel);
    auto* historicalLayout = new QHBoxLayout(historicalBanner_);
    historicalBannerTitle_ = new QLabel(historicalBanner_);
    historicalBannerTitle_->setWordWrap(true);
    auto historicalFont = historicalBannerTitle_->font();
    historicalFont.setBold(true);
    historicalBannerTitle_->setFont(historicalFont);
    returnToCurrentWorkButton_ = new QPushButton(tr("Return to Current Work"), historicalBanner_);
    returnToCurrentWorkButton_->setObjectName(
        QStringLiteral("convergenceReturnToCurrentWorkButton"));
    historicalLayout->addWidget(historicalBannerTitle_, 1);
    historicalLayout->addWidget(returnToCurrentWorkButton_);
    historicalBanner_->hide();
    auto* heading = new QLabel(tr("Segmentation Convergence Study"), this);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);
    auto* description = new QLabel(tr(
        "Run the same model repeatedly while increasing every wire's segmentation. Sources, loads, "
        "and transmission-line endpoints are remapped by physical position. The authored source is "
        "not modified. Stable results support numerical model adequacy; they do not prove correctness."), this);
    description->setWordWrap(true);

    auto* controls = new QFormLayout;
    frequencyControl_ = new QDoubleSpinBox(this);
    frequencyControl_->setRange(0.000001, 1.0e9);
    frequencyControl_->setDecimals(DisplayDecimalPlaces);
    frequencyControl_->setSuffix(tr(" MHz"));
    increaseControl_ = new QSpinBox(this);
    increaseControl_->setRange(10, 100);
    increaseControl_->setValue(33);
    increaseControl_->setSuffix(tr(" % per level"));
    levelsControl_ = new QSpinBox(this);
    levelsControl_->setRange(3, 8);
    levelsControl_->setValue(4);
    impedanceToleranceControl_ = new QDoubleSpinBox(this);
    impedanceToleranceControl_->setRange(0.01, 25.0);
    impedanceToleranceControl_->setDecimals(DisplayDecimalPlaces);
    impedanceToleranceControl_->setValue(1.0);
    impedanceToleranceControl_->setSuffix(tr(" %"));
    gainToleranceControl_ = new QDoubleSpinBox(this);
    gainToleranceControl_->setRange(0.001, 5.0);
    gainToleranceControl_->setDecimals(DisplayDecimalPlaces);
    gainToleranceControl_->setValue(0.05);
    gainToleranceControl_->setSuffix(tr(" dB"));
    controls->addRow(tr("Test frequency:"), frequencyControl_);
    controls->addRow(tr("Segment increase:"), increaseControl_);
    controls->addRow(tr("Segmentation levels:"), levelsControl_);
    controls->addRow(tr("Impedance-change tolerance:"), impedanceToleranceControl_);
    controls->addRow(tr("Peak-gain-change tolerance:"), gainToleranceControl_);

    auto* buttons = new QHBoxLayout;
    runButton_ = new QPushButton(tr("Run Convergence Study"), this);
    runButton_->setObjectName(QStringLiteral("runConvergenceStudyButton"));
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setEnabled(false);
    progress_ = new QProgressBar(this);
    buttons->addWidget(runButton_);
    buttons->addWidget(cancelButton_);
    buttons->addWidget(progress_, 1);
    statusLabel_ = new QLabel(tr("Check a valid model and select a runnable solver."), this);
    statusLabel_->setWordWrap(true);
    conclusionLabel_ = new QLabel(tr("No convergence study is loaded."), this);
    conclusionLabel_->setWordWrap(true);
    auto conclusionFont = conclusionLabel_->font();
    conclusionFont.setBold(true);
    conclusionLabel_->setFont(conclusionFont);

    resultsTable_ = new QTableWidget(this);
    resultsTable_->setColumnCount(ResultColumnCount);
    resultsTable_->setHorizontalHeaderLabels({tr("Scale"), tr("Segments"), tr("R (Ω)"),
        tr("X (Ω)"), tr("ΔZ (%)"), tr("Sampled Peak Gain (dBi)"), tr("ΔGain (dB)"),
        tr("Assessment"), tr("Run")});
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable_->verticalHeader()->hide();
    resultsTable_->horizontalHeader()->setSectionResizeMode(AssessmentColumn, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(RunColumn, QHeaderView::ResizeToContents);

    layout->addWidget(historicalBanner_);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addLayout(controls);
    layout->addLayout(buttons);
    layout->addWidget(statusLabel_);
    layout->addWidget(conclusionLabel_);
    layout->addWidget(resultsTable_, 1);
    connect(runButton_, &QPushButton::clicked, this, [this] { startStudy(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancelStudy(); });
    connect(returnToCurrentWorkButton_, &QPushButton::clicked, this, [this] {
        if (returnToCurrentWorkCallback_) returnToCurrentWorkCallback_();
    });
    updateReadiness();
}

void ConvergenceWorkspace::setContext(QString source, QString sourceFile, QString backend,
    QString executable, int timeoutSeconds, bool modelValid)
{
    if (isRunning() || historicalSession_) return;
    source_ = std::move(source);
    sourceFile_ = std::move(sourceFile);
    backend_ = std::move(backend);
    executable_ = std::move(executable);
    timeoutSeconds_ = timeoutSeconds;
    modelValid_ = modelValid;
    const auto resolution = nec::NecSymbolResolver{}.resolve(source_.toStdString());
    if (resolution.ok()) {
        numericSource_ = QString::fromStdString(resolution.generatedDeck);
        const auto setup = nec::NecSetupConverter{}.convert(
            nec::NecParser{}.parse(resolution.generatedDeck));
        if (setup.frequency) frequencyControl_->setValue(setup.frequency->startMHz);
        const auto parsed = analysis::prepareSegmentationConvergenceInput(
            resolution.generatedDeck, frequencyControl_->value(), 1.0);
        if (!parsed.ok()) statusLabel_->setText(QString::fromStdString(parsed.error));
    } else {
        numericSource_.clear();
    }
    updateReadiness();
}

void ConvergenceWorkspace::setExternalRunActive(bool active)
{
    externalRunActive_ = active;
    updateReadiness();
}

void ConvergenceWorkspace::setRunsChangedCallback(std::function<void()> callback)
{
    runsChangedCallback_ = std::move(callback);
}

void ConvergenceWorkspace::setRunningChangedCallback(std::function<void()> callback)
{
    runningChangedCallback_ = std::move(callback);
}

void ConvergenceWorkspace::setSummaryChangedCallback(
    std::function<void(const QString&)> callback)
{
    summaryChangedCallback_ = std::move(callback);
}

void ConvergenceWorkspace::setReturnToCurrentWorkCallback(std::function<void()> callback)
{
    returnToCurrentWorkCallback_ = std::move(callback);
}

auto ConvergenceWorkspace::isRunning() const noexcept -> bool
{
    return process_ != nullptr || stepIndex_ < steps_.size();
}

void ConvergenceWorkspace::updateReadiness()
{
    const QFileInfo executable(executable_);
    const auto ready = !historicalSession_ && modelValid_ && !numericSource_.isEmpty() && !externalRunActive_
        && !isRunning() && analysis::isBackendRunnable(backend_.toStdString())
        && executable.exists() && executable.isFile() && executable.isExecutable();
    runButton_->setEnabled(ready);
    cancelButton_->setEnabled(isRunning());
    for (auto* control : std::array<QWidget*, 5>{frequencyControl_, increaseControl_, levelsControl_,
             impedanceToleranceControl_, gainToleranceControl_})
        control->setEnabled(!isRunning() && !historicalSession_);
}

void ConvergenceWorkspace::startStudy()
{
    if (!runButton_->isEnabled()) return;
    const auto resolution = nec::NecSymbolResolver{}.resolve(source_.toStdString());
    if (!resolution.ok()) {
        statusLabel_->setText(tr("Resolve symbolic expression errors before convergence testing."));
        return;
    }
    numericSource_ = QString::fromStdString(resolution.generatedDeck);
    const auto resolvedDocument = nec::NecParser{}.parse(resolution.generatedDeck);
    trackGain_ = std::ranges::any_of(resolvedDocument.cards(),
        [](const auto& card) { return card.kind == nec::NecCardKind::RadiationPattern; });
    steps_.clear();
    const auto levels = levelsControl_->value();
    const auto factor = 1.0 + increaseControl_->value() / 100.0;
    for (auto index = 0; index < levels; ++index) {
        const auto scale = std::pow(factor, index);
        const auto prepared = analysis::prepareSegmentationConvergenceInput(
            resolution.generatedDeck, frequencyControl_->value(), scale);
        if (!prepared.ok()) {
            statusLabel_->setText(QString::fromStdString(prepared.error));
            steps_.clear();
            return;
        }
        Step step;
        step.scale = scale;
        step.totalSegments = prepared.totalSegments;
        step.row = index;
        step.warning = QString::fromStdString(prepared.warning);
        steps_.push_back(std::move(step));
    }

    sessionRecord_ = runStore_.create(backend_, sourceFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : sourceFile_, QStringLiteral("convergence-session"));
    sessionRecord_->status = QStringLiteral("Running");
    sessionRecord_->candidateCount = levels;
    sessionRecord_->summary = tr("Segmentation convergence · %1 MHz · %2 levels")
        .arg(formatDecimal(frequencyControl_->value())).arg(levels);
    runStore_.save(*sessionRecord_);
    const QJsonObject metadata{{QStringLiteral("version"), 1},
        {QStringLiteral("frequencyMHz"), frequencyControl_->value()},
        {QStringLiteral("increasePercent"), increaseControl_->value()},
        {QStringLiteral("levels"), levels},
        {QStringLiteral("impedanceTolerancePercent"), impedanceToleranceControl_->value()},
        {QStringLiteral("gainToleranceDb"), gainToleranceControl_->value()},
        {QStringLiteral("trackGain"), trackGain_}};
    writeFile(QDir(sessionRecord_->directory).filePath(QStringLiteral("convergence-session.json")),
        QJsonDocument(metadata).toJson(QJsonDocument::Indented));
    writeFile(QDir(sessionRecord_->directory).filePath(QStringLiteral("model.source.nec")),
        source_.toUtf8());
    resetTable(levels);
    for (const auto& step : steps_) {
        resultsTable_->setItem(step.row, ScaleColumn, numericItem(step.scale));
        resultsTable_->setItem(step.row, SegmentsColumn,
            new QTableWidgetItem(QString::number(step.totalSegments)));
        setStepStatus(step.row, tr("Pending"));
    }
    stepIndex_ = 0;
    cancelRequested_ = false;
    modelChangedDuringRun_ = false;
    progress_->setRange(0, levels);
    progress_->setValue(0);
    conclusionLabel_->setText(tr("Study in progress…"));
    statusLabel_->setText(tr("Running %1 segmentation levels at %2 MHz.")
        .arg(levels).arg(formatDecimal(frequencyControl_->value())));
    if (summaryChangedCallback_) summaryChangedCallback_(tr("Convergence study running…"));
    updateReadiness();
    if (runningChangedCallback_) runningChangedCallback_();
    startNextStep();
}

void ConvergenceWorkspace::cancelStudy()
{
    cancelRequested_ = true;
    cancelButton_->setEnabled(false);
    if (process_ != nullptr) process_->terminate();
}

void ConvergenceWorkspace::cancelAndWait()
{
    cancelRequested_ = true;
    if (process_ != nullptr) {
        process_->kill();
        process_->waitForFinished(2000);
    }
}

void ConvergenceWorkspace::cancel()
{
    cancelStudy();
}

void ConvergenceWorkspace::clearResults()
{
    if (isRunning()) return;
    hasResults_ = false;
    resetTable(0);
    conclusionLabel_->setText(tr("No convergence study is loaded."));
}

void ConvergenceWorkspace::markStale()
{
    if (isRunning()) {
        modelChangedDuringRun_ = true;
        return;
    }
    if (hasResults_)
        conclusionLabel_->setText(tr("STALE — the model changed after this convergence study."));
}

void ConvergenceWorkspace::startNextStep()
{
    if (cancelRequested_ || stepIndex_ >= steps_.size()) {
        finishStudy();
        return;
    }
    auto& step = steps_[stepIndex_];
    const auto prepared = analysis::prepareSegmentationConvergenceInput(
        numericSource_.toStdString(), frequencyControl_->value(), step.scale);
    step.record = runStore_.create(backend_, sourceFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : sourceFile_, QStringLiteral("convergence-step"),
        sessionRecord_ ? sessionRecord_->id : QString{});
    if (!prepared.ok() || !writeStepFiles(step, prepared.deck)) {
        step.record.status = QStringLiteral("Failed");
        runStore_.save(step.record);
        setStepStatus(step.row, prepared.ok() ? tr("Could not write run files")
                                               : QString::fromStdString(prepared.error));
        ++stepIndex_;
        progress_->setValue(static_cast<int>(stepIndex_));
        QTimer::singleShot(0, this, [this] { startNextStep(); });
        return;
    }
    if (!step.warning.isEmpty()) {
        step.record.status = QStringLiteral("Skipped");
        step.record.summary = step.warning;
        runStore_.save(step.record);
        setStepStatus(step.row, tr("Outside recommended limits · %1").arg(step.warning));
        ++stepIndex_;
        progress_->setValue(static_cast<int>(stepIndex_));
        QTimer::singleShot(0, this, [this] { startNextStep(); });
        return;
    }

    analysis::SolverCommand command;
    try {
        command = analysis::buildSolverCommand(backend_.toStdString(), executable_.toStdString(),
            "model.nec", "model.out");
    } catch (const std::exception& error) {
        step.record.status = QStringLiteral("Failed");
        runStore_.save(step.record);
        setStepStatus(step.row, QString::fromLocal8Bit(error.what()));
        ++stepIndex_;
        QTimer::singleShot(0, this, [this] { startNextStep(); });
        return;
    }

    step.record.status = QStringLiteral("Running");
    runStore_.save(step.record);
    setStepStatus(step.row, tr("Running"));
    timedOut_ = false;
    elapsed_.restart();
    process_ = new QProcess(this);
    auto* process = process_;
    process->setWorkingDirectory(step.record.directory);
    process->setProgram(QString::fromStdString(command.executable));
    QStringList arguments;
    for (const auto& argument : command.arguments) arguments.append(QString::fromStdString(argument));
    process->setArguments(arguments);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process_ == process && error == QProcess::FailedToStart)
            finishCurrentStep(false, process->errorString());
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process](int exitCode, QProcess::ExitStatus status) {
            if (process_ == process)
                finishCurrentStep(status == QProcess::NormalExit && exitCode == 0);
        });
    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this, process] {
        if (process_ == process) {
            timedOut_ = true;
            process->kill();
        }
    });
    timeout_->start(timeoutSeconds_ * 1000);
    process->start();
}

void ConvergenceWorkspace::finishCurrentStep(bool processSucceeded, const QString& detail)
{
    if (process_ == nullptr || stepIndex_ >= steps_.size()) return;
    if (timeout_ != nullptr) {
        timeout_->stop();
        timeout_->deleteLater();
        timeout_ = nullptr;
    }
    auto* process = process_;
    const auto processLog = process->readAll();
    process->deleteLater();
    process_ = nullptr;
    auto& step = steps_[stepIndex_];
    writeFile(QDir(step.record.directory).filePath(QStringLiteral("run.log")), processLog);
    step.record.durationSeconds = elapsed_.elapsed() / 1000.0;
    QFile outputFile(QDir(step.record.directory).filePath(QStringLiteral("model.out")));
    const auto output = outputFile.open(QIODevice::ReadOnly) ? outputFile.readAll() : QByteArray{};
    step.record.outputBytes = output.size();
    if (processSucceeded && !cancelRequested_ && !output.isEmpty()) {
        populateStepResult(step, output);
    } else {
        const auto status = cancelRequested_ ? tr("Canceled") : timedOut_ ? tr("Timed out")
            : detail.isEmpty() ? tr("Solver failed") : detail;
        setStepStatus(step.row, status);
        step.record.status = cancelRequested_ ? QStringLiteral("Canceled")
            : timedOut_ ? QStringLiteral("Timed Out") : QStringLiteral("Failed");
    }
    runStore_.save(step.record);
    ++stepIndex_;
    progress_->setValue(static_cast<int>(stepIndex_));
    QTimer::singleShot(0, this, [this] { startNextStep(); });
}

void ConvergenceWorkspace::populateStepResult(Step& step, const QByteArray& output)
{
    const auto result = analysis::NecOutputParser{}.parse(output.toStdString());
    const auto* feedpoint = nearestFeedpoint(result, frequencyControl_->value());
    if (feedpoint == nullptr) {
        step.record.status = QStringLiteral("Failed");
        setStepStatus(step.row, tr("No impedance result"));
        return;
    }
    step.impedance = feedpoint->impedance;
    step.peakGainDb = peakGain(result, frequencyControl_->value());
    step.record.status = QStringLiteral("Completed");
    step.record.frequencyCount = 1;
    step.record.hasImpedance = true;
    step.record.hasRadiation = step.peakGainDb.has_value();
    resultsTable_->setItem(step.row, ResistanceColumn, numericItem(step.impedance->real()));
    resultsTable_->setItem(step.row, ReactanceColumn, numericItem(step.impedance->imag()));
    resultsTable_->setItem(step.row, GainColumn, optionalItem(step.peakGainDb));

    std::optional<double> impedanceChange;
    std::optional<double> gainChange;
    if (stepIndex_ > 0) {
        const auto& previous = steps_[stepIndex_ - 1];
        if (previous.impedance) {
            impedanceChange = 100.0 * std::abs(*step.impedance - *previous.impedance)
                / std::max(std::abs(*previous.impedance), 1.0e-12);
        }
        if (step.peakGainDb && previous.peakGainDb)
            gainChange = std::abs(*step.peakGainDb - *previous.peakGainDb);
    }
    resultsTable_->setItem(step.row, ImpedanceChangeColumn, optionalItem(impedanceChange));
    resultsTable_->setItem(step.row, GainChangeColumn, optionalItem(gainChange));
    if (!impedanceChange) {
        setStepStatus(step.row, tr("Baseline"));
    } else {
        step.withinTolerance = *impedanceChange <= impedanceToleranceControl_->value()
            && (!trackGain_ || (gainChange && *gainChange <= gainToleranceControl_->value()));
        setStepStatus(step.row, trackGain_ && !gainChange
            ? tr("Gain comparison unavailable")
            : step.withinTolerance ? tr("Within selected tolerances") : tr("Still changing"));
    }
    step.record.summary = tr("%1 segments · R %2 Ω · X %3 Ω")
        .arg(step.totalSegments).arg(formatDecimal(step.impedance->real()),
            formatDecimal(step.impedance->imag()));
}

void ConvergenceWorkspace::finishStudy()
{
    if (cancelRequested_) {
        for (std::size_t index = stepIndex_; index < steps_.size(); ++index)
            setStepStatus(steps_[index].row, tr("Skipped"));
        conclusionLabel_->setText(tr("Convergence study canceled."));
    } else {
        const auto finalTwoStable = steps_.size() >= 3
            && steps_[steps_.size() - 1].withinTolerance
            && steps_[steps_.size() - 2].withinTolerance;
        conclusionLabel_->setText(finalTwoStable
            ? tr("Stable across the final two refinements within the selected tolerances.")
            : tr("Not yet stable across two consecutive refinements. Review the trend or add levels."));
    }
    hasResults_ = !cancelRequested_;
    if (modelChangedDuringRun_ && !cancelRequested_)
        conclusionLabel_->setText(tr("STALE — the model changed while this study was running."));
    statusLabel_->setText(cancelRequested_ ? tr("Study canceled.") : tr("Study complete."));
    if (sessionRecord_) {
        sessionRecord_->status = cancelRequested_ ? QStringLiteral("Canceled") : QStringLiteral("Completed");
        sessionRecord_->summary = conclusionLabel_->text();
        runStore_.save(*sessionRecord_);
        if (runsChangedCallback_) runsChangedCallback_();
        sessionRecord_.reset();
    }
    if (summaryChangedCallback_) summaryChangedCallback_(conclusionLabel_->text());
    steps_.clear();
    stepIndex_ = 0;
    updateReadiness();
    if (runningChangedCallback_) runningChangedCallback_();
}

auto ConvergenceWorkspace::writeStepFiles(Step& step, const std::string& deck) -> bool
{
    const QDir directory(step.record.directory);
    const QJsonObject metadata{{QStringLiteral("version"), 1},
        {QStringLiteral("scale"), step.scale}, {QStringLiteral("totalSegments"), step.totalSegments},
        {QStringLiteral("frequencyMHz"), frequencyControl_->value()},
        {QStringLiteral("warning"), step.warning}};
    return writeFile(directory.filePath(QStringLiteral("model.source.nec")), source_.toUtf8())
        && writeFile(directory.filePath(QStringLiteral("model.nec")), QByteArray::fromStdString(deck))
        && writeFile(directory.filePath(QStringLiteral("convergence.json")),
            QJsonDocument(metadata).toJson(QJsonDocument::Indented));
}

void ConvergenceWorkspace::setStepStatus(int row, const QString& status)
{
    resultsTable_->setItem(row, AssessmentColumn, new QTableWidgetItem(status));
    if (row >= 0 && static_cast<std::size_t>(row) < steps_.size()
        && !steps_[static_cast<std::size_t>(row)].record.id.isEmpty()) {
        resultsTable_->setItem(row, RunColumn,
            new QTableWidgetItem(steps_[static_cast<std::size_t>(row)].record.id));
    }
}

void ConvergenceWorkspace::resetTable(int rows)
{
    resultsTable_->clearContents();
    resultsTable_->setRowCount(rows);
}

auto ConvergenceWorkspace::loadSession(const QString& sessionId) -> bool
{
    const auto records = runStore_.load();
    const auto session = std::ranges::find(records, sessionId, &AnalysisRunRecord::id);
    if (session == records.end() || session->runType != QStringLiteral("convergence-session"))
        return false;
    QFile sessionFile(QDir(session->directory).filePath(QStringLiteral("convergence-session.json")));
    const auto metadata = sessionFile.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(sessionFile.readAll()).object() : QJsonObject{};
    frequencyControl_->setValue(metadata.value(QStringLiteral("frequencyMHz")).toDouble());
    increaseControl_->setValue(metadata.value(QStringLiteral("increasePercent")).toInt(33));
    levelsControl_->setValue(metadata.value(QStringLiteral("levels")).toInt(4));
    impedanceToleranceControl_->setValue(
        metadata.value(QStringLiteral("impedanceTolerancePercent")).toDouble(1.0));
    gainToleranceControl_->setValue(metadata.value(QStringLiteral("gainToleranceDb")).toDouble(0.05));
    trackGain_ = metadata.value(QStringLiteral("trackGain")).toBool();

    std::vector<AnalysisRunRecord> stepRecords;
    for (const auto& record : records) {
        if (record.parentId == sessionId && record.runType == QStringLiteral("convergence-step"))
            stepRecords.push_back(record);
    }
    std::ranges::sort(stepRecords, {}, &AnalysisRunRecord::started);
    steps_.clear();
    resetTable(static_cast<int>(stepRecords.size()));
    for (std::size_t index = 0; index < stepRecords.size(); ++index) {
        QFile stepFile(QDir(stepRecords[index].directory).filePath(QStringLiteral("convergence.json")));
        const auto stepMetadata = stepFile.open(QIODevice::ReadOnly)
            ? QJsonDocument::fromJson(stepFile.readAll()).object() : QJsonObject{};
        Step restoredStep;
        restoredStep.scale = stepMetadata.value(QStringLiteral("scale")).toDouble(1.0);
        restoredStep.totalSegments = stepMetadata.value(QStringLiteral("totalSegments")).toInt();
        restoredStep.row = static_cast<int>(index);
        restoredStep.record = stepRecords[index];
        restoredStep.warning = stepMetadata.value(QStringLiteral("warning")).toString();
        steps_.push_back(std::move(restoredStep));
        auto& step = steps_.back();
        resultsTable_->setItem(step.row, ScaleColumn, numericItem(step.scale));
        resultsTable_->setItem(step.row, SegmentsColumn,
            new QTableWidgetItem(QString::number(step.totalSegments)));
        QFile outputFile(QDir(step.record.directory).filePath(QStringLiteral("model.out")));
        if (outputFile.open(QIODevice::ReadOnly)) {
            stepIndex_ = index;
            populateStepResult(step, outputFile.readAll());
        } else {
            setStepStatus(step.row, step.warning.isEmpty() ? step.record.status
                : tr("Outside recommended limits · %1").arg(step.warning));
        }
    }
    stepIndex_ = steps_.size();
    historicalSession_ = true;
    const auto modelName = session->sourceFile.isEmpty()
        ? tr("Archived model.nec") : QFileInfo(session->sourceFile).fileName();
    historicalBannerTitle_->setText(tr(
        "Historical Convergence Session — %1 — %2 — %3\nArchived and read-only; it cannot be rerun in place.")
        .arg(modelName, session->started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            session->backend.isEmpty() ? tr("Unknown backend") : session->backend));
    historicalBanner_->show();
    runButton_->setText(tr("Historical Session — Read Only"));
    conclusionLabel_->setText(session->summary.isEmpty()
        ? tr("No convergence conclusion is available.") : session->summary);
    statusLabel_->setText(tr("Historical convergence study · %1").arg(session->status));
    hasResults_ = true;
    progress_->setRange(0, static_cast<int>(steps_.size()));
    progress_->setValue(static_cast<int>(steps_.size()));
    if (summaryChangedCallback_) summaryChangedCallback_(conclusionLabel_->text());
    steps_.clear();
    stepIndex_ = 0;
    updateReadiness();
    return true;
}

void ConvergenceWorkspace::leaveHistoricalSession()
{
    if (!historicalSession_) return;
    historicalSession_ = false;
    historicalBanner_->hide();
    runButton_->setText(tr("Run Convergence Study"));
    updateReadiness();
}

}
