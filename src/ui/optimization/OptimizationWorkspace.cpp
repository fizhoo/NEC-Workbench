#include "ui/optimization/OptimizationWorkspace.h"

#include "analysis/AnalysisResult.h"
#include "analysis/NecOutputParser.h"
#include "analysis/SolverCommand.h"
#include "analysis/SolverInput.h"
#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
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
#include <cmath>
#include <exception>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>

namespace necwb::ui {
namespace {

enum ResultColumn {
    ValueColumn,
    SwrColumn,
    ResistanceColumn,
    ReactanceColumn,
    StatusColumn,
    RunColumn,
    ResultColumnCount
};

auto numericItem(double value, int precision = 6) -> QTableWidgetItem*
{
    auto* item = new QTableWidgetItem(QString::number(value, 'g', precision));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

auto writeFile(const QString& path, const QByteArray& data) -> bool
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

}

OptimizationWorkspace::OptimizationWorkspace(QWidget* parent)
    : QWidget(parent), bestScore_(std::numeric_limits<double>::infinity())
{
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(tr("Basic Parameter Optimization"), this);
    auto headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 3);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    auto* description = new QLabel(tr(
        "Sweep one SY variable and select the candidate with the lowest worst-case SWR."), this);
    description->setWordWrap(true);

    variablesTable_ = new QTableWidget(this);
    variablesTable_->setColumnCount(4);
    variablesTable_->setHorizontalHeaderLabels(
        {tr("Symbol"), tr("Expression"), tr("Current Value"), tr("Line")});
    variablesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    variablesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    variablesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    variablesTable_->verticalHeader()->hide();
    variablesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    variablesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    variablesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    variablesTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    auto* controls = new QFormLayout;
    variableControl_ = new QComboBox(this);
    minimumControl_ = new QDoubleSpinBox(this);
    maximumControl_ = new QDoubleSpinBox(this);
    pointsControl_ = new QSpinBox(this);
    referenceImpedanceControl_ = new QDoubleSpinBox(this);
    for (auto* control : {minimumControl_, maximumControl_}) {
        control->setRange(-1.0e12, 1.0e12);
        control->setDecimals(9);
    }
    pointsControl_->setRange(2, 101);
    pointsControl_->setValue(7);
    referenceImpedanceControl_->setRange(1.0, 10000.0);
    referenceImpedanceControl_->setDecimals(2);
    referenceImpedanceControl_->setValue(50.0);
    referenceImpedanceControl_->setSuffix(QStringLiteral(" Ω"));
    controls->addRow(tr("Variable:"), variableControl_);
    controls->addRow(tr("Minimum:"), minimumControl_);
    controls->addRow(tr("Maximum:"), maximumControl_);
    controls->addRow(tr("Candidate points:"), pointsControl_);
    controls->addRow(tr("Reference impedance:"), referenceImpedanceControl_);

    auto* buttons = new QHBoxLayout;
    runButton_ = new QPushButton(tr("Run SWR Sweep"), this);
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setEnabled(false);
    progress_ = new QProgressBar(this);
    progress_->setTextVisible(true);
    buttons->addWidget(runButton_);
    buttons->addWidget(cancelButton_);
    buttons->addWidget(progress_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    bestLabel_ = new QLabel(tr("No optimization results yet."), this);
    bestLabel_->setWordWrap(true);
    resultsTable_ = new QTableWidget(this);
    resultsTable_->setColumnCount(ResultColumnCount);
    resultsTable_->setHorizontalHeaderLabels({tr("Value"), tr("Worst SWR"), tr("R (Ω)"),
        tr("X (Ω)"), tr("Status"), tr("Run")});
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable_->verticalHeader()->hide();
    resultsTable_->horizontalHeader()->setSectionResizeMode(StatusColumn, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(RunColumn, QHeaderView::ResizeToContents);

    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addWidget(variablesTable_, 1);
    layout->addLayout(controls);
    layout->addLayout(buttons);
    layout->addWidget(statusLabel_);
    layout->addWidget(bestLabel_);
    layout->addWidget(resultsTable_, 1);

    connect(variableControl_, &QComboBox::currentIndexChanged, this, [this] { updateBounds(); });
    connect(variablesTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        const auto* item = variablesTable_->item(row, 0);
        const auto index = item == nullptr ? -1 : variableControl_->findText(item->text());
        if (index >= 0) variableControl_->setCurrentIndex(index);
    });
    connect(runButton_, &QPushButton::clicked, this, [this] { startSweep(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancelSweep(); });
    updateReadiness();
}

void OptimizationWorkspace::setContext(QString source, QString sourceFile, QString backend,
    QString executable, int timeoutSeconds, bool modelValid)
{
    if (isRunning()) return;
    source_ = std::move(source);
    sourceFile_ = std::move(sourceFile);
    backend_ = std::move(backend);
    executable_ = std::move(executable);
    timeoutSeconds_ = timeoutSeconds;
    modelValid_ = modelValid;
    populateVariables(nec::NecSymbolResolver{}.resolve(source_.toStdString()));
    updateReadiness();
}

void OptimizationWorkspace::setExternalRunActive(bool active)
{
    externalRunActive_ = active;
    updateReadiness();
}

void OptimizationWorkspace::setModelValid(bool valid)
{
    modelValid_ = valid;
    updateReadiness();
}

void OptimizationWorkspace::setRunsChangedCallback(std::function<void()> callback)
{
    runsChangedCallback_ = std::move(callback);
}

void OptimizationWorkspace::setRunningChangedCallback(std::function<void()> callback)
{
    runningChangedCallback_ = std::move(callback);
}

auto OptimizationWorkspace::isRunning() const noexcept -> bool
{
    return process_ != nullptr || candidateIndex_ < candidates_.size();
}

void OptimizationWorkspace::cancelAndWait()
{
    cancelRequested_ = true;
    if (process_ != nullptr) {
        process_->kill();
        process_->waitForFinished(2000);
    }
}

void OptimizationWorkspace::populateVariables(const nec::SymbolResolution& resolution)
{
    definitions_ = resolution.definitions;
    variablesTable_->setRowCount(static_cast<int>(definitions_.size()));
    variableControl_->clear();
    for (std::size_t index = 0; index < definitions_.size(); ++index) {
        const auto& definition = definitions_[index];
        const auto row = static_cast<int>(index);
        variablesTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(definition.name)));
        variablesTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(definition.expression)));
        variablesTable_->setItem(row, 2, numericItem(definition.value, 12));
        variablesTable_->setItem(row, 3, numericItem(static_cast<double>(definition.lineNumber), 12));
        variableControl_->addItem(QString::fromStdString(definition.name), definition.value);
    }
    statusLabel_->setText(resolution.ok()
        ? definitions_.empty() ? tr("Add SY declarations to enable parameter optimization.")
                               : tr("Choose one symbol and a bounded range.")
        : tr("Resolve the model's SY expression errors before optimizing."));
    updateBounds();
}

void OptimizationWorkspace::updateBounds()
{
    if (variableControl_->currentIndex() < 0) return;
    const auto value = variableControl_->currentData().toDouble();
    auto first = value == 0.0 ? -1.0 : value * 0.8;
    auto second = value == 0.0 ? 1.0 : value * 1.2;
    if (first > second) std::swap(first, second);
    minimumControl_->setValue(first);
    maximumControl_->setValue(second);
    updateReadiness();
}

void OptimizationWorkspace::updateReadiness()
{
    const auto executable = QFileInfo(executable_);
    const auto ready = modelValid_ && !definitions_.empty() && !externalRunActive_
        && !isRunning() && analysis::isBackendRunnable(backend_.toStdString())
        && executable.exists() && executable.isFile() && executable.isExecutable();
    runButton_->setEnabled(ready);
    cancelButton_->setEnabled(isRunning());
}

void OptimizationWorkspace::startSweep()
{
    if (!runButton_->isEnabled() || minimumControl_->value() >= maximumControl_->value()) {
        statusLabel_->setText(tr("Minimum must be less than maximum."));
        return;
    }
    selectedSymbol_ = variableControl_->currentText();
    candidates_.clear();
    resultsTable_->setRowCount(pointsControl_->value());
    const auto minimum = minimumControl_->value();
    const auto maximum = maximumControl_->value();
    const auto count = pointsControl_->value();
    for (auto index = 0; index < count; ++index) {
        const auto fraction = static_cast<double>(index) / static_cast<double>(count - 1);
        Candidate candidate{minimum + fraction * (maximum - minimum), index, {}};
        candidates_.push_back(candidate);
        resultsTable_->setItem(index, ValueColumn, numericItem(candidate.value, 12));
        setCandidateStatus(index, tr("Pending"));
    }
    candidateIndex_ = 0;
    bestScore_ = std::numeric_limits<double>::infinity();
    bestRow_ = -1;
    cancelRequested_ = false;
    progress_->setRange(0, count);
    progress_->setValue(0);
    bestLabel_->setText(tr("Sweep in progress…"));
    statusLabel_->setText(tr("Running %1 candidates for %2.").arg(count).arg(selectedSymbol_));
    updateReadiness();
    if (runningChangedCallback_) runningChangedCallback_();
    startNextCandidate();
}

void OptimizationWorkspace::cancelSweep()
{
    cancelRequested_ = true;
    cancelButton_->setEnabled(false);
    if (process_ != nullptr) process_->terminate();
}

void OptimizationWorkspace::startNextCandidate()
{
    if (cancelRequested_ || candidateIndex_ >= candidates_.size()) {
        finishSweep();
        return;
    }
    auto& candidate = candidates_[candidateIndex_];
    const auto resolution = nec::NecSymbolResolver{}.resolve(source_.toStdString(),
        {{selectedSymbol_.toStdString(), candidate.value}});
    if (!resolution.ok()) {
        setCandidateStatus(candidate.row, tr("Expression error"));
        ++candidateIndex_;
        progress_->setValue(static_cast<int>(candidateIndex_));
        QTimer::singleShot(0, this, [this] { startNextCandidate(); });
        return;
    }
    const auto candidateCheck = nec::NecModelChecker{}.check(
        nec::NecParser{}.parse(resolution.generatedDeck));
    if (candidateCheck.errorCount() != 0) {
        setCandidateStatus(candidate.row, tr("Invalid candidate model"));
        ++candidateIndex_;
        progress_->setValue(static_cast<int>(candidateIndex_));
        QTimer::singleShot(0, this, [this] { startNextCandidate(); });
        return;
    }
    candidate.record = runStore_.create(backend_, sourceFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : sourceFile_);
    if (!writeCandidateFiles(candidate, resolution.generatedDeck)) {
        candidate.record.status = QStringLiteral("Failed");
        runStore_.save(candidate.record);
        setCandidateStatus(candidate.row, tr("Could not write run files"));
        if (runsChangedCallback_) runsChangedCallback_();
        ++candidateIndex_;
        progress_->setValue(static_cast<int>(candidateIndex_));
        QTimer::singleShot(0, this, [this] { startNextCandidate(); });
        return;
    }

    analysis::SolverCommand command;
    try {
        command = analysis::buildSolverCommand(backend_.toStdString(), executable_.toStdString(),
            "model.nec", "model.out");
    } catch (const std::exception& error) {
        setCandidateStatus(candidate.row, QString::fromLocal8Bit(error.what()));
        candidate.record.status = QStringLiteral("Failed");
        runStore_.save(candidate.record);
        if (runsChangedCallback_) runsChangedCallback_();
        ++candidateIndex_;
        progress_->setValue(static_cast<int>(candidateIndex_));
        QTimer::singleShot(0, this, [this] { startNextCandidate(); });
        return;
    }

    candidate.record.status = QStringLiteral("Running");
    runStore_.save(candidate.record);
    setCandidateStatus(candidate.row, tr("Running"));
    timedOut_ = false;
    elapsed_.restart();
    process_ = new QProcess(this);
    auto* process = process_;
    process_->setWorkingDirectory(candidate.record.directory);
    process_->setProgram(QString::fromStdString(command.executable));
    QStringList arguments;
    for (const auto& argument : command.arguments) arguments.append(QString::fromStdString(argument));
    process_->setArguments(arguments);
    connect(process_, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process_ == process && error == QProcess::FailedToStart)
            finishCurrentCandidate(false, process->errorString());
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process](int exitCode, QProcess::ExitStatus status) {
            if (process_ == process)
                finishCurrentCandidate(status == QProcess::NormalExit && exitCode == 0);
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
    process_->start();
}

void OptimizationWorkspace::finishCurrentCandidate(bool processSucceeded, const QString& detail)
{
    if (process_ == nullptr || candidateIndex_ >= candidates_.size()) return;
    if (timeout_ != nullptr) {
        timeout_->stop();
        timeout_->deleteLater();
        timeout_ = nullptr;
    }
    process_->deleteLater();
    process_ = nullptr;
    auto& candidate = candidates_[candidateIndex_];
    candidate.record.durationSeconds = elapsed_.elapsed() / 1000.0;
    const auto outputPath = QDir(candidate.record.directory).filePath(QStringLiteral("model.out"));
    QFile outputFile(outputPath);
    QByteArray output;
    if (outputFile.open(QIODevice::ReadOnly)) output = outputFile.readAll();
    candidate.record.outputBytes = output.size();

    if (processSucceeded && !cancelRequested_) {
        const auto result = analysis::NecOutputParser{}.parse(output.toStdString());
        const analysis::FeedpointResult* worst{};
        auto worstSwr = -std::numeric_limits<double>::infinity();
        std::set<double> frequencies;
        for (const auto& feedpoint : result.feedpoints) {
            frequencies.insert(feedpoint.frequencyMHz);
            const auto swr = analysis::standingWaveRatio(
                feedpoint.impedance, referenceImpedanceControl_->value());
            if (std::isfinite(swr) && swr > worstSwr) {
                worstSwr = swr;
                worst = &feedpoint;
            }
        }
        if (worst != nullptr) {
            resultsTable_->setItem(candidate.row, SwrColumn, numericItem(worstSwr, 8));
            resultsTable_->setItem(candidate.row, ResistanceColumn, numericItem(worst->impedance.real(), 8));
            resultsTable_->setItem(candidate.row, ReactanceColumn, numericItem(worst->impedance.imag(), 8));
            setCandidateStatus(candidate.row, tr("Completed"));
            candidate.record.status = QStringLiteral("Completed");
            candidate.record.frequencyCount = static_cast<int>(frequencies.size());
            candidate.record.hasImpedance = true;
            if (worstSwr < bestScore_) {
                bestScore_ = worstSwr;
                bestRow_ = candidate.row;
            }
        } else {
            setCandidateStatus(candidate.row, tr("No impedance results"));
            candidate.record.status = QStringLiteral("Failed");
        }
    } else {
        const auto status = cancelRequested_ ? tr("Canceled")
            : timedOut_ ? tr("Timed out")
            : detail.isEmpty() ? tr("Solver failed") : detail;
        setCandidateStatus(candidate.row, status);
        candidate.record.status = cancelRequested_ ? QStringLiteral("Canceled")
            : timedOut_ ? QStringLiteral("Timed Out") : QStringLiteral("Failed");
    }
    runStore_.save(candidate.record);
    if (runsChangedCallback_) runsChangedCallback_();
    ++candidateIndex_;
    progress_->setValue(static_cast<int>(candidateIndex_));
    QTimer::singleShot(0, this, [this] { startNextCandidate(); });
}

void OptimizationWorkspace::finishSweep()
{
    if (cancelRequested_) {
        for (std::size_t index = candidateIndex_; index < candidates_.size(); ++index)
            setCandidateStatus(candidates_[index].row, tr("Skipped"));
        statusLabel_->setText(tr("Optimization sweep canceled."));
    } else {
        statusLabel_->setText(tr("Optimization sweep complete."));
    }
    candidateIndex_ = candidates_.size();
    if (bestRow_ >= 0) {
        auto font = resultsTable_->item(bestRow_, ValueColumn)->font();
        font.setBold(true);
        for (auto column = 0; column < ResultColumnCount; ++column) {
            if (auto* item = resultsTable_->item(bestRow_, column)) item->setFont(font);
        }
        bestLabel_->setText(tr("Best candidate: %1 = %2, worst SWR %3")
            .arg(selectedSymbol_)
            .arg(candidates_[static_cast<std::size_t>(bestRow_)].value, 0, 'g', 12)
            .arg(bestScore_, 0, 'g', 8));
    } else {
        bestLabel_->setText(tr("No successful candidate produced impedance results."));
    }
    candidates_.clear();
    candidateIndex_ = 0;
    updateReadiness();
    if (runningChangedCallback_) runningChangedCallback_();
}

auto OptimizationWorkspace::writeCandidateFiles(Candidate& candidate,
    const std::string& generatedDeck) -> bool
{
    const QDir directory(candidate.record.directory);
    const auto numericDeck = analysis::prepareImpedanceInput(generatedDeck);
    const auto metadata = QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("variable"), selectedSymbol_},
        {QStringLiteral("value"), candidate.value},
        {QStringLiteral("objective"), QStringLiteral("minimum-worst-swr")},
        {QStringLiteral("referenceImpedance"), referenceImpedanceControl_->value()},
    };
    return writeFile(directory.filePath(QStringLiteral("model.source.nec")), source_.toUtf8())
        && writeFile(directory.filePath(QStringLiteral("model.nec")), QByteArray::fromStdString(numericDeck))
        && writeFile(directory.filePath(QStringLiteral("optimization.json")),
            QJsonDocument(metadata).toJson(QJsonDocument::Indented));
}

void OptimizationWorkspace::setCandidateStatus(int row, const QString& status)
{
    resultsTable_->setItem(row, StatusColumn, new QTableWidgetItem(status));
    if (row >= 0 && static_cast<std::size_t>(row) < candidates_.size()
        && !candidates_[static_cast<std::size_t>(row)].record.id.isEmpty()) {
        resultsTable_->setItem(row, RunColumn,
            new QTableWidgetItem(candidates_[static_cast<std::size_t>(row)].record.id));
    }
}

}
