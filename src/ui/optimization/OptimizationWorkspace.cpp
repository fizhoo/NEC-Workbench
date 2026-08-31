#include "ui/optimization/OptimizationWorkspace.h"

#include "analysis/AnalysisResult.h"
#include "analysis/NecOutputParser.h"
#include "analysis/OptimizationObjective.h"
#include "analysis/SolverCommand.h"
#include "analysis/SolverInput.h"
#include "nec/NecModelChecker.h"
#include "nec/DeckGeometryUnits.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <charconv>
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
    FrequencyColumn,
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

auto asciiLower(std::string value) -> std::string
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

auto directGeometrySymbols(const nec::NecDocument& document) -> std::unordered_set<std::string>
{
    std::unordered_set<std::string> result;
    for (const auto& card : document.cards()) {
        if (card.kind != nec::NecCardKind::GeometryWire) continue;
        for (auto fieldIndex = std::size_t{2}; fieldIndex < card.fields.size(); ++fieldIndex) {
            const auto& field = card.fields[fieldIndex];
            double numericValue{};
            const auto [numericEnd, numericError] = std::from_chars(
                field.data(), field.data() + field.size(), numericValue);
            if (numericError == std::errc{} && numericEnd == field.data() + field.size()) continue;
            for (auto position = std::size_t{}; position < field.size();) {
                if (!std::isalpha(static_cast<unsigned char>(field[position]))
                    && field[position] != '_') {
                    ++position;
                    continue;
                }
                const auto start = position++;
                while (position < field.size()
                    && (std::isalnum(static_cast<unsigned char>(field[position]))
                        || field[position] == '_')) ++position;
                result.insert(asciiLower(field.substr(start, position - start)));
            }
        }
    }
    return result;
}

auto frequencyAt(const model::FrequencyDefinition& frequency, int index) -> double
{
    return frequency.steppingMode == 1
        ? frequency.startMHz * std::pow(frequency.step, index)
        : frequency.startMHz + index * frequency.step;
}

auto frequencyArray(std::span<const double> frequenciesMHz) -> QJsonArray
{
    QJsonArray result;
    for (const auto frequencyMHz : frequenciesMHz) result.append(frequencyMHz);
    return result;
}

}

OptimizationWorkspace::OptimizationWorkspace(QWidget* parent)
    : QWidget(parent), bestScore_(std::numeric_limits<double>::infinity())
{
    auto* layout = new QVBoxLayout(this);
    historicalBanner_ = new QFrame(this);
    historicalBanner_->setObjectName(QStringLiteral("optimizationHistoricalBanner"));
    static_cast<QFrame*>(historicalBanner_)->setFrameShape(QFrame::StyledPanel);
    auto* historicalLayout = new QHBoxLayout(historicalBanner_);
    historicalBannerTitle_ = new QLabel(historicalBanner_);
    historicalBannerTitle_->setWordWrap(true);
    auto historicalFont = historicalBannerTitle_->font();
    historicalFont.setBold(true);
    historicalBannerTitle_->setFont(historicalFont);
    returnToCurrentWorkButton_ = new QPushButton(tr("Return to Current Work"), historicalBanner_);
    returnToCurrentWorkButton_->setObjectName(
        QStringLiteral("optimizationReturnToCurrentWorkButton"));
    historicalLayout->addWidget(historicalBannerTitle_, 1);
    historicalLayout->addWidget(returnToCurrentWorkButton_);
    historicalBanner_->hide();
    auto* heading = new QLabel(tr("Parameter Sweep"), this);
    auto headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 3);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    auto* description = new QLabel(tr(
        "Evaluate a bounded set of values for one SY variable and rank the candidates by SWR. "
        "This is an exhaustive parameter sweep, not an iterative optimizer. Optimizer convergence "
        "and post-design tolerance analysis are separate future tools."), this);
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
    objectiveControl_ = new QComboBox(this);
    objectiveControl_->addItem(tr("Minimize Worst SWR Across Frequencies"),
        static_cast<int>(analysis::OptimizationObjectiveKind::MaximumSwr));
    objectiveControl_->addItem(tr("Minimize SWR at Selected Frequency"),
        static_cast<int>(analysis::OptimizationObjectiveKind::SwrAtFrequency));
    targetFrequencyControl_ = new QDoubleSpinBox(this);
    targetFrequencyControl_->setRange(0.000001, 1.0e9);
    targetFrequencyControl_->setDecimals(6);
    targetFrequencyControl_->setValue(14.175);
    targetFrequencyControl_->setSuffix(tr(" MHz"));
    frequencyModeControl_ = new QComboBox(this);
    frequencyModeControl_->setObjectName(QStringLiteral("optimizationFrequencyMode"));
    frequencyModeControl_->addItem(tr("Use Model FR Sweep"),
        static_cast<int>(FrequencyMode::ModelSweep));
    frequencyModeControl_->addItem(tr("Use Selected Frequencies"),
        static_cast<int>(FrequencyMode::Explicit));
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
    controls->addRow(tr("Objective:"), objectiveControl_);
    controls->addRow(tr("Selected frequency:"), targetFrequencyControl_);
    controls->addRow(tr("Frequency source:"), frequencyModeControl_);
    controls->addRow(tr("Minimum:"), minimumControl_);
    controls->addRow(tr("Maximum:"), maximumControl_);
    controls->addRow(tr("Candidate points:"), pointsControl_);
    controls->addRow(tr("Reference impedance:"), referenceImpedanceControl_);

    explicitFrequencyPanel_ = new QWidget(this);
    auto* frequencyLayout = new QHBoxLayout(explicitFrequencyPanel_);
    frequencyLayout->setContentsMargins(0, 0, 0, 0);
    frequencyTable_ = new QTableWidget(explicitFrequencyPanel_);
    frequencyTable_->setObjectName(QStringLiteral("optimizationFrequencyTable"));
    frequencyTable_->setColumnCount(1);
    frequencyTable_->setHorizontalHeaderLabels({tr("Frequency (MHz)")});
    frequencyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    frequencyTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    frequencyTable_->verticalHeader()->hide();
    frequencyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    frequencyTable_->setMaximumHeight(132);
    auto* frequencyButtons = new QVBoxLayout;
    frequencyEntryControl_ = new QDoubleSpinBox(explicitFrequencyPanel_);
    frequencyEntryControl_->setObjectName(QStringLiteral("optimizationFrequencyEntry"));
    frequencyEntryControl_->setRange(0.000001, 1.0e9);
    frequencyEntryControl_->setDecimals(6);
    frequencyEntryControl_->setValue(14.175);
    frequencyEntryControl_->setSuffix(tr(" MHz"));
    addFrequencyButton_ = new QPushButton(tr("Add Frequency"), explicitFrequencyPanel_);
    addFrequencyButton_->setObjectName(QStringLiteral("optimizationAddFrequency"));
    removeFrequencyButton_ = new QPushButton(tr("Remove Selected"), explicitFrequencyPanel_);
    removeFrequencyButton_->setObjectName(QStringLiteral("optimizationRemoveFrequency"));
    pasteFrequencyButton_ = new QPushButton(tr("Paste List…"), explicitFrequencyPanel_);
    pasteFrequencyButton_->setObjectName(QStringLiteral("optimizationPasteFrequencies"));
    frequencyButtons->addWidget(frequencyEntryControl_);
    frequencyButtons->addWidget(addFrequencyButton_);
    frequencyButtons->addWidget(removeFrequencyButton_);
    frequencyButtons->addWidget(pasteFrequencyButton_);
    frequencyButtons->addStretch();
    frequencyLayout->addWidget(frequencyTable_, 1);
    frequencyLayout->addLayout(frequencyButtons);
    workloadLabel_ = new QLabel(this);
    workloadLabel_->setObjectName(QStringLiteral("optimizationWorkload"));

    auto* buttons = new QHBoxLayout;
    runButton_ = new QPushButton(tr("Run Parameter Sweep"), this);
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setEnabled(false);
    progress_ = new QProgressBar(this);
    progress_->setTextVisible(true);
    buttons->addWidget(runButton_);
    buttons->addWidget(cancelButton_);
    buttons->addWidget(progress_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    bestLabel_ = new QLabel(tr("No parameter-sweep results yet."), this);
    bestLabel_->setWordWrap(true);
    resultsTable_ = new QTableWidget(this);
    resultsTable_->setColumnCount(ResultColumnCount);
    resultsTable_->setHorizontalHeaderLabels({tr("Value"), tr("Maximum SWR"), tr("Frequency"),
        tr("R (Ω)"), tr("X (Ω)"), tr("Status"), tr("Run")});
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable_->verticalHeader()->hide();
    resultsTable_->horizontalHeader()->setSectionResizeMode(StatusColumn, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(RunColumn, QHeaderView::ResizeToContents);

    candidateDetailLabel_ = new QLabel(tr("Select a completed candidate to inspect every frequency."), this);
    candidateDetailLabel_->setObjectName(QStringLiteral("optimizationCandidateDetailLabel"));
    candidateDetailsTable_ = new QTableWidget(this);
    candidateDetailsTable_->setObjectName(QStringLiteral("optimizationCandidateDetails"));
    candidateDetailsTable_->setColumnCount(4);
    candidateDetailsTable_->setHorizontalHeaderLabels(
        {tr("Frequency (MHz)"), tr("SWR"), tr("R (Ω)"), tr("X (Ω)")});
    candidateDetailsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    candidateDetailsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    candidateDetailsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    candidateDetailsTable_->verticalHeader()->hide();
    candidateDetailsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    auto* detailPanel = new QWidget(this);
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(candidateDetailLabel_);
    detailLayout->addWidget(candidateDetailsTable_);
    auto* resultSplitter = new QSplitter(Qt::Vertical, this);
    resultSplitter->addWidget(resultsTable_);
    resultSplitter->addWidget(detailPanel);
    resultSplitter->setStretchFactor(0, 2);
    resultSplitter->setStretchFactor(1, 1);

    layout->addWidget(historicalBanner_);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addWidget(variablesTable_, 1);
    layout->addLayout(controls);
    layout->addWidget(explicitFrequencyPanel_);
    layout->addWidget(workloadLabel_);
    layout->addLayout(buttons);
    layout->addWidget(statusLabel_);
    layout->addWidget(bestLabel_);
    layout->addWidget(resultSplitter, 2);

    connect(variableControl_, &QComboBox::currentIndexChanged, this, [this] { updateBounds(); });
    connect(objectiveControl_, &QComboBox::currentIndexChanged,
        this, [this] { updateObjectiveControls(); });
    connect(frequencyModeControl_, &QComboBox::currentIndexChanged,
        this, [this] { updateFrequencyControls(); });
    connect(pointsControl_, &QSpinBox::valueChanged, this, [this] { updateWorkload(); });
    connect(frequencyTable_, &QTableWidget::itemChanged, this, [this] {
        updateWorkload();
        updateReadiness();
    });
    connect(addFrequencyButton_, &QPushButton::clicked, this, [this] {
        addExplicitFrequency(frequencyEntryControl_->value());
    });
    connect(removeFrequencyButton_, &QPushButton::clicked, this, [this] {
        std::set<int, std::greater<>> rows;
        for (const auto* item : frequencyTable_->selectedItems()) rows.insert(item->row());
        for (const auto row : rows) frequencyTable_->removeRow(row);
        updateWorkload();
        updateReadiness();
    });
    connect(pasteFrequencyButton_, &QPushButton::clicked,
        this, [this] { pasteExplicitFrequencies(); });
    connect(resultsTable_, &QTableWidget::itemSelectionChanged,
        this, [this] { updateCandidateDetails(); });
    connect(variablesTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        const auto* item = variablesTable_->item(row, 0);
        const auto index = item == nullptr ? -1 : variableControl_->findText(item->text());
        if (index >= 0) variableControl_->setCurrentIndex(index);
    });
    connect(runButton_, &QPushButton::clicked, this, [this] { startSweep(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancelSweep(); });
    connect(returnToCurrentWorkButton_, &QPushButton::clicked, this, [this] {
        if (returnToCurrentWorkCallback_) returnToCurrentWorkCallback_();
    });
    updateObjectiveControls();
    updateFrequencyControls();
}

void OptimizationWorkspace::setContext(QString source, QString sourceFile, QString backend,
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
    geometrySymbols_ = directGeometrySymbols(nec::NecParser{}.parse(source_.toStdString()));
    deckLengthSuffix_.clear();
    if (resolution.ok()) {
        const auto units = nec::inspectDeckGeometryUnits(
            nec::NecParser{}.parse(resolution.resolvedSource));
        if (units.uniform && units.standardUnit) {
            const auto symbol = model::lengthUnitSymbol(*units.standardUnit);
            deckLengthSuffix_ = QStringLiteral(" %1").arg(QString::fromLatin1(
                symbol.data(), static_cast<qsizetype>(symbol.size())));
        } else if (units.uniform) {
            deckLengthSuffix_ = tr(" deck units");
        }
    }
    populateVariables(resolution);
    if (resolution.ok()) {
        const auto setup = nec::NecSetupConverter{}.convert(
            nec::NecParser{}.parse(resolution.generatedDeck));
        if (setup.frequency) {
            targetFrequencyControl_->setValue(setup.frequency->startMHz);
            frequencyEntryControl_->setValue(setup.frequency->startMHz);
            populateModelFrequencies(*setup.frequency);
        } else {
            modelFrequenciesMHz_.clear();
            frequencyTable_->setRowCount(0);
        }
    } else {
        modelFrequenciesMHz_.clear();
        frequencyTable_->setRowCount(0);
    }
    candidates_.clear();
    candidateIndex_ = 0;
    candidateDetailsTable_->setRowCount(0);
    candidateDetailLabel_->setText(
        tr("Select a completed candidate to inspect every frequency."));
    updateWorkload();
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

void OptimizationWorkspace::setReturnToCurrentWorkCallback(std::function<void()> callback)
{
    returnToCurrentWorkCallback_ = std::move(callback);
}

auto OptimizationWorkspace::isRunning() const noexcept -> bool
{
    return process_ != nullptr || candidateIndex_ < candidates_.size();
}

auto OptimizationWorkspace::loadSession(const QString& sessionId) -> bool
{
    auto records = runStore_.load();
    const auto session = std::ranges::find(records, sessionId, &AnalysisRunRecord::id);
    if (session == records.end() || session->runType != QStringLiteral("optimization-session")) return false;
    std::vector<AnalysisRunRecord> candidateRecords;
    for (const auto& record : records) {
        if (record.parentId == sessionId) candidateRecords.push_back(record);
    }
    std::ranges::sort(candidateRecords, {}, &AnalysisRunRecord::started);

    QFile sessionMetadataFile(QDir(session->directory).filePath(
        QStringLiteral("optimization-session.json")));
    const auto sessionMetadata = sessionMetadataFile.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(sessionMetadataFile.readAll()).object() : QJsonObject{};
    selectedSymbol_ = sessionMetadata.value(QStringLiteral("variable")).toString();
    activeFrequenciesMHz_.clear();
    for (const auto value : sessionMetadata.value(QStringLiteral("frequenciesMHz")).toArray()) {
        const auto frequencyMHz = value.toDouble();
        if (std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            activeFrequenciesMHz_.push_back(frequencyMHz);
    }
    const auto restoredMode = sessionMetadata.value(QStringLiteral("frequencyMode")).toString()
            == QStringLiteral("explicit")
        ? FrequencyMode::Explicit : FrequencyMode::ModelSweep;
    const auto modeIndex = frequencyModeControl_->findData(static_cast<int>(restoredMode));
    if (modeIndex >= 0) frequencyModeControl_->setCurrentIndex(modeIndex);
    if (restoredMode == FrequencyMode::Explicit && !activeFrequenciesMHz_.empty())
        setExplicitFrequencies(activeFrequenciesMHz_);

    candidates_.clear();
    resultsTable_->setRowCount(static_cast<int>(candidateRecords.size()));
    bestScore_ = std::numeric_limits<double>::infinity();
    bestRow_ = -1;
    auto restoredObjective = analysis::OptimizationObjectiveSpec{};
    for (std::size_t index = 0; index < candidateRecords.size(); ++index) {
        const auto row = static_cast<int>(index);
        QFile metadataFile(QDir(candidateRecords[index].directory).filePath(
            QStringLiteral("optimization.json")));
        const auto metadata = metadataFile.open(QIODevice::ReadOnly)
            ? QJsonDocument::fromJson(metadataFile.readAll()).object() : QJsonObject{};
        const auto value = metadata.value(QStringLiteral("value")).toDouble();
        if (selectedSymbol_.isEmpty()) selectedSymbol_ = metadata.value(QStringLiteral("variable")).toString();
        selectedValueSuffix_ = metadata.value(QStringLiteral("unit")).toString();
        if (!selectedValueSuffix_.isEmpty()) selectedValueSuffix_.prepend(' ');
        resultsTable_->setItem(row, ValueColumn, numericItem(value, 12));
        QFile outputFile(QDir(candidateRecords[index].directory).filePath(QStringLiteral("model.out")));
        analysis::AnalysisResult result;
        if (outputFile.open(QIODevice::ReadOnly))
            result = analysis::NecOutputParser{}.parse(outputFile.readAll().toStdString());
        const auto objectiveId = metadata.value(QStringLiteral("objective")).toString();
        restoredObjective = {
            .kind = objectiveId == QStringLiteral("minimize-swr-at-frequency")
                ? analysis::OptimizationObjectiveKind::SwrAtFrequency
                : analysis::OptimizationObjectiveKind::MaximumSwr,
            .referenceImpedance = metadata.value(
                QStringLiteral("referenceImpedance")).toDouble(50.0),
            .targetFrequencyMHz = metadata.value(
                QStringLiteral("targetFrequencyMHz")).toDouble(),
        };
        const auto evaluation = analysis::evaluateOptimizationObjective(
            result.feedpoints, restoredObjective);
        if (evaluation && evaluation->feedpoint) {
            resultsTable_->setItem(row, SwrColumn, numericItem(evaluation->swr, 8));
            resultsTable_->setItem(row, FrequencyColumn,
                numericItem(evaluation->feedpoint->frequencyMHz, 10));
            resultsTable_->setItem(row, ResistanceColumn,
                numericItem(evaluation->feedpoint->impedance.real(), 8));
            resultsTable_->setItem(row, ReactanceColumn,
                numericItem(evaluation->feedpoint->impedance.imag(), 8));
            if (evaluation->score < bestScore_) {
                bestScore_ = evaluation->score;
                bestRow_ = row;
            }
        }
        resultsTable_->setItem(row, StatusColumn, new QTableWidgetItem(candidateRecords[index].status));
        resultsTable_->setItem(row, RunColumn, new QTableWidgetItem(candidateRecords[index].id));
        candidates_.push_back(Candidate{value, row, candidateRecords[index], result.feedpoints});
    }
    historicalSession_ = true;
    const auto modelName = session->sourceFile.isEmpty()
        ? tr("Archived model.nec") : QFileInfo(session->sourceFile).fileName();
    historicalBannerTitle_->setText(tr(
        "Historical Optimization Session — %1 — %2 — %3\nArchived and read-only; it cannot be rerun in place.")
        .arg(modelName, session->started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            session->backend.isEmpty() ? tr("Unknown backend") : session->backend));
    historicalBanner_->show();
    runButton_->setText(tr("Historical Session — Read Only"));
    statusLabel_->setText(tr("Historical optimization session · %1").arg(session->status));
    bestLabel_->setText(session->summary.isEmpty() ? tr("No optimization summary is available.") : session->summary);
    activeObjective_ = restoredObjective;
    resultsTable_->horizontalHeaderItem(SwrColumn)->setText(objectiveName(activeObjective_.kind));
    progress_->setRange(0, static_cast<int>(candidateRecords.size()));
    progress_->setValue(static_cast<int>(candidateRecords.size()));
    candidateIndex_ = candidates_.size();
    if (bestRow_ >= 0) {
        resultsTable_->selectRow(bestRow_);
        updateCandidateDetails();
    }
    updateWorkload();
    updateReadiness();
    return true;
}

void OptimizationWorkspace::leaveHistoricalSession()
{
    if (!historicalSession_) return;
    historicalSession_ = false;
    historicalBanner_->hide();
    runButton_->setText(tr("Run Parameter Sweep"));
    updateReadiness();
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
        auto* valueItem = numericItem(definition.value, 12);
        const auto isGeometry = geometrySymbols_.contains(asciiLower(definition.name));
        if (isGeometry) valueItem->setText(valueItem->text() + deckLengthSuffix_);
        variablesTable_->setItem(row, 2, valueItem);
        variablesTable_->setItem(row, 3, numericItem(static_cast<double>(definition.lineNumber), 12));
        variableControl_->addItem(QString::fromStdString(definition.name), definition.value);
        variableControl_->setItemData(row, isGeometry, Qt::UserRole + 1);
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
    selectedValueSuffix_ = variableControl_->currentData(Qt::UserRole + 1).toBool()
        ? deckLengthSuffix_ : QString{};
    minimumControl_->setSuffix(selectedValueSuffix_);
    maximumControl_->setSuffix(selectedValueSuffix_);
    resultsTable_->horizontalHeaderItem(ValueColumn)->setText(
        selectedValueSuffix_.isEmpty() ? tr("Value")
                                       : tr("Value (%1)").arg(selectedValueSuffix_.trimmed()));
    auto first = value == 0.0 ? -1.0 : value * 0.8;
    auto second = value == 0.0 ? 1.0 : value * 1.2;
    if (first > second) std::swap(first, second);
    minimumControl_->setValue(first);
    maximumControl_->setValue(second);
    updateReadiness();
}

void OptimizationWorkspace::updateObjectiveControls()
{
    const auto objective = selectedObjective();
    targetFrequencyControl_->setEnabled(
        !isRunning() && objective.kind == analysis::OptimizationObjectiveKind::SwrAtFrequency);
    resultsTable_->horizontalHeaderItem(SwrColumn)->setText(objectiveName(objective.kind));
}

void OptimizationWorkspace::updateFrequencyControls()
{
    const auto explicitMode = selectedFrequencyMode() == FrequencyMode::Explicit;
    explicitFrequencyPanel_->setVisible(explicitMode);
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::updateWorkload()
{
    const auto frequencyCount = selectedFrequencyMode() == FrequencyMode::Explicit
        ? explicitFrequencies().size() : modelFrequenciesMHz_.size();
    const auto candidateCount = pointsControl_->value();
    workloadLabel_->setText(tr("%1 candidates × %2 frequencies = %3 calculated points")
        .arg(candidateCount)
        .arg(frequencyCount)
        .arg(static_cast<qulonglong>(candidateCount * frequencyCount)));
}

void OptimizationWorkspace::populateModelFrequencies(const model::FrequencyDefinition& frequency)
{
    modelFrequenciesMHz_.clear();
    const auto count = std::max(1, frequency.count);
    modelFrequenciesMHz_.reserve(static_cast<std::size_t>(count));
    for (auto index = 0; index < count; ++index) {
        const auto frequencyMHz = frequencyAt(frequency, index);
        if (std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            modelFrequenciesMHz_.push_back(frequencyMHz);
    }
    setExplicitFrequencies(modelFrequenciesMHz_);
}

void OptimizationWorkspace::setExplicitFrequencies(
    const std::vector<double>& frequenciesMHz)
{
    const QSignalBlocker blocker(frequencyTable_);
    frequencyTable_->setRowCount(static_cast<int>(frequenciesMHz.size()));
    for (std::size_t index = 0; index < frequenciesMHz.size(); ++index)
        frequencyTable_->setItem(static_cast<int>(index), 0,
            numericItem(frequenciesMHz[index], 12));
    updateWorkload();
    updateReadiness();
}

void OptimizationWorkspace::addExplicitFrequency(double frequencyMHz)
{
    if (!std::isfinite(frequencyMHz) || frequencyMHz <= 0.0) return;
    const auto row = frequencyTable_->rowCount();
    frequencyTable_->insertRow(row);
    frequencyTable_->setItem(row, 0, numericItem(frequencyMHz, 12));
}

void OptimizationWorkspace::pasteExplicitFrequencies()
{
    bool accepted{};
    const auto text = QInputDialog::getMultiLineText(this, tr("Paste Frequencies"),
        tr("Enter MHz values separated by spaces, commas, semicolons, or new lines:"),
        {}, &accepted);
    if (!accepted) return;
    const auto values = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
        Qt::SkipEmptyParts);
    std::vector<double> frequenciesMHz;
    frequenciesMHz.reserve(static_cast<std::size_t>(values.size()));
    for (const auto& value : values) {
        bool valid{};
        const auto frequencyMHz = value.toDouble(&valid);
        if (valid && std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            frequenciesMHz.push_back(frequencyMHz);
    }
    setExplicitFrequencies(frequenciesMHz);
}

void OptimizationWorkspace::updateCandidateDetails()
{
    candidateDetailsTable_->setRowCount(0);
    const auto row = resultsTable_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= candidates_.size()
        || candidates_[static_cast<std::size_t>(row)].feedpoints.empty()) {
        candidateDetailLabel_->setText(
            tr("Select a completed candidate to inspect every frequency."));
        return;
    }

    const auto& candidate = candidates_[static_cast<std::size_t>(row)];
    auto feedpoints = candidate.feedpoints;
    std::ranges::sort(feedpoints, {}, &analysis::FeedpointResult::frequencyMHz);
    const auto evaluation = analysis::evaluateOptimizationObjective(feedpoints, activeObjective_);
    candidateDetailsTable_->setRowCount(static_cast<int>(feedpoints.size()));
    for (std::size_t index = 0; index < feedpoints.size(); ++index) {
        const auto& feedpoint = feedpoints[index];
        const auto detailRow = static_cast<int>(index);
        candidateDetailsTable_->setItem(detailRow, 0, numericItem(feedpoint.frequencyMHz, 10));
        candidateDetailsTable_->setItem(detailRow, 1, numericItem(
            analysis::standingWaveRatio(feedpoint.impedance, activeObjective_.referenceImpedance), 8));
        candidateDetailsTable_->setItem(detailRow, 2, numericItem(feedpoint.impedance.real(), 8));
        candidateDetailsTable_->setItem(detailRow, 3, numericItem(feedpoint.impedance.imag(), 8));
        if (evaluation && evaluation->feedpoint
            && std::abs(evaluation->feedpoint->frequencyMHz - feedpoint.frequencyMHz) < 1.0e-9) {
            for (auto column = 0; column < candidateDetailsTable_->columnCount(); ++column) {
                auto* item = candidateDetailsTable_->item(detailRow, column);
                auto font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
        }
    }
    candidateDetailLabel_->setText(tr("Candidate %1 = %2%3 · %4 frequencies · objective point bold")
        .arg(selectedSymbol_.isEmpty() ? tr("value") : selectedSymbol_)
        .arg(candidate.value, 0, 'g', 12)
        .arg(selectedValueSuffix_)
        .arg(feedpoints.size()));
}

void OptimizationWorkspace::updateReadiness()
{
    const auto executable = QFileInfo(executable_);
    const auto hasFrequencies = selectedFrequencyMode() == FrequencyMode::Explicit
        ? !explicitFrequencies().empty() : !modelFrequenciesMHz_.empty();
    const auto ready = !historicalSession_ && modelValid_ && !definitions_.empty() && !externalRunActive_
        && hasFrequencies
        && !isRunning() && analysis::isBackendRunnable(backend_.toStdString())
        && executable.exists() && executable.isFile() && executable.isExecutable();
    runButton_->setEnabled(ready);
    cancelButton_->setEnabled(isRunning());
    const auto editable = !isRunning() && !historicalSession_;
    variablesTable_->setEnabled(editable);
    variableControl_->setEnabled(editable);
    objectiveControl_->setEnabled(editable);
    frequencyModeControl_->setEnabled(editable);
    minimumControl_->setEnabled(editable);
    maximumControl_->setEnabled(editable);
    pointsControl_->setEnabled(editable);
    referenceImpedanceControl_->setEnabled(editable);
    targetFrequencyControl_->setEnabled(editable
        && selectedObjective().kind == analysis::OptimizationObjectiveKind::SwrAtFrequency);
    const auto explicitMode = selectedFrequencyMode() == FrequencyMode::Explicit;
    frequencyTable_->setEnabled(editable && explicitMode);
    frequencyEntryControl_->setEnabled(editable && explicitMode);
    addFrequencyButton_->setEnabled(editable && explicitMode);
    removeFrequencyButton_->setEnabled(editable && explicitMode);
    pasteFrequencyButton_->setEnabled(editable && explicitMode);
}

void OptimizationWorkspace::startSweep()
{
    if (!runButton_->isEnabled() || minimumControl_->value() >= maximumControl_->value()) {
        statusLabel_->setText(tr("Minimum must be less than maximum."));
        return;
    }
    selectedSymbol_ = variableControl_->currentText();
    activeObjective_ = selectedObjective();
    activeFrequenciesMHz_ = selectedFrequencyMode() == FrequencyMode::Explicit
        ? explicitFrequencies() : modelFrequenciesMHz_;
    if (activeFrequenciesMHz_.empty()) {
        statusLabel_->setText(tr("Choose at least one valid frequency."));
        return;
    }
    resultsTable_->horizontalHeaderItem(SwrColumn)->setText(objectiveName(activeObjective_.kind));
    sessionRecord_ = runStore_.create(backend_, sourceFile_.isEmpty()
        ? QStringLiteral("Untitled model.nec") : sourceFile_,
        QStringLiteral("optimization-session"));
    sessionRecord_->status = QStringLiteral("Running");
    sessionRecord_->candidateCount = pointsControl_->value();
    sessionRecord_->summary = tr("Optimize %1 · %2 · %3 candidates × %4 frequencies")
        .arg(selectedSymbol_, objectiveName(activeObjective_.kind))
        .arg(pointsControl_->value())
        .arg(activeFrequenciesMHz_.size());
    runStore_.save(*sessionRecord_);
    const auto sessionMetadata = QJsonObject{
        {QStringLiteral("version"), 2},
        {QStringLiteral("variable"), selectedSymbol_},
        {QStringLiteral("minimum"), minimumControl_->value()},
        {QStringLiteral("maximum"), maximumControl_->value()},
        {QStringLiteral("candidateCount"), pointsControl_->value()},
        {QStringLiteral("objective"), activeObjective_.kind
            == analysis::OptimizationObjectiveKind::MaximumSwr
                ? QStringLiteral("minimize-maximum-swr")
                : QStringLiteral("minimize-swr-at-frequency")},
        {QStringLiteral("referenceImpedance"), activeObjective_.referenceImpedance},
        {QStringLiteral("targetFrequencyMHz"), activeObjective_.targetFrequencyMHz},
        {QStringLiteral("frequencyMode"), selectedFrequencyMode() == FrequencyMode::Explicit
            ? QStringLiteral("explicit") : QStringLiteral("model-fr")},
        {QStringLiteral("frequenciesMHz"), frequencyArray(activeFrequenciesMHz_)},
    };
    writeFile(QDir(sessionRecord_->directory).filePath(QStringLiteral("optimization-session.json")),
        QJsonDocument(sessionMetadata).toJson(QJsonDocument::Indented));
    candidates_.clear();
    resultsTable_->clearSelection();
    candidateDetailsTable_->setRowCount(0);
    candidateDetailLabel_->setText(
        tr("Select a completed candidate to inspect every frequency."));
    resultsTable_->setRowCount(pointsControl_->value());
    const auto minimum = minimumControl_->value();
    const auto maximum = maximumControl_->value();
    const auto count = pointsControl_->value();
    for (auto index = 0; index < count; ++index) {
        const auto fraction = static_cast<double>(index) / static_cast<double>(count - 1);
        Candidate candidate{minimum + fraction * (maximum - minimum), index, {}, {}};
        candidates_.push_back(candidate);
        auto* valueItem = numericItem(candidate.value, 12);
        valueItem->setText(valueItem->text() + selectedValueSuffix_);
        resultsTable_->setItem(index, ValueColumn, valueItem);
        setCandidateStatus(index, tr("Pending"));
    }
    candidateIndex_ = 0;
    bestScore_ = std::numeric_limits<double>::infinity();
    bestRow_ = -1;
    cancelRequested_ = false;
    progress_->setRange(0, count);
    progress_->setValue(0);
    bestLabel_->setText(tr("Sweep in progress…"));
    statusLabel_->setText(tr("Running %1 candidates × %2 frequencies for %3.")
        .arg(count).arg(activeFrequenciesMHz_.size()).arg(selectedSymbol_));
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
        ? QStringLiteral("Untitled model.nec") : sourceFile_,
        QStringLiteral("optimization-candidate"), sessionRecord_ ? sessionRecord_->id : QString{});
    if (!writeCandidateFiles(candidate, resolution.generatedDeck)) {
        candidate.record.status = QStringLiteral("Failed");
        runStore_.save(candidate.record);
        setCandidateStatus(candidate.row, tr("Could not write run files"));
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
        candidate.feedpoints = result.feedpoints;
        std::set<double> frequencies;
        for (const auto& feedpoint : result.feedpoints) frequencies.insert(feedpoint.frequencyMHz);
        const auto evaluation = analysis::evaluateOptimizationObjective(
            result.feedpoints, activeObjective_);
        if (evaluation && evaluation->feedpoint) {
            resultsTable_->setItem(candidate.row, SwrColumn, numericItem(evaluation->swr, 8));
            resultsTable_->setItem(candidate.row, FrequencyColumn,
                numericItem(evaluation->feedpoint->frequencyMHz, 10));
            resultsTable_->setItem(candidate.row, ResistanceColumn,
                numericItem(evaluation->feedpoint->impedance.real(), 8));
            resultsTable_->setItem(candidate.row, ReactanceColumn,
                numericItem(evaluation->feedpoint->impedance.imag(), 8));
            setCandidateStatus(candidate.row, tr("Completed"));
            candidate.record.status = QStringLiteral("Completed");
            candidate.record.frequencyCount = static_cast<int>(frequencies.size());
            candidate.record.hasImpedance = true;
            if (evaluation->score < bestScore_) {
                bestScore_ = evaluation->score;
                bestRow_ = candidate.row;
            }
            if (resultsTable_->currentRow() == candidate.row) updateCandidateDetails();
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
    ++candidateIndex_;
    progress_->setValue(static_cast<int>(candidateIndex_));
    if (sessionRecord_) {
        sessionRecord_->summary = tr("Optimize %1 · %2/%3 candidates complete")
            .arg(selectedSymbol_).arg(candidateIndex_).arg(candidates_.size());
        runStore_.save(*sessionRecord_);
    }
    QTimer::singleShot(0, this, [this] { startNextCandidate(); });
}

void OptimizationWorkspace::finishSweep()
{
    if (cancelRequested_) {
        for (std::size_t index = candidateIndex_; index < candidates_.size(); ++index)
            setCandidateStatus(candidates_[index].row, tr("Skipped"));
        statusLabel_->setText(tr("Parameter sweep canceled."));
    } else {
        statusLabel_->setText(tr("Parameter sweep complete."));
    }
    candidateIndex_ = candidates_.size();
    if (bestRow_ >= 0) {
        auto font = resultsTable_->item(bestRow_, ValueColumn)->font();
        font.setBold(true);
        for (auto column = 0; column < ResultColumnCount; ++column) {
            if (auto* item = resultsTable_->item(bestRow_, column)) item->setFont(font);
        }
        bestLabel_->setText(tr("Best candidate: %1 = %2%3, %4 %5")
            .arg(selectedSymbol_)
            .arg(candidates_[static_cast<std::size_t>(bestRow_)].value, 0, 'g', 12)
            .arg(selectedValueSuffix_)
            .arg(objectiveName(activeObjective_.kind))
            .arg(bestScore_, 0, 'g', 8));
    } else {
        bestLabel_->setText(tr("No successful candidate produced impedance results."));
    }
    if (sessionRecord_) {
        sessionRecord_->status = cancelRequested_ ? QStringLiteral("Canceled") : QStringLiteral("Completed");
        sessionRecord_->summary = bestLabel_->text();
        runStore_.save(*sessionRecord_);
        if (runsChangedCallback_) runsChangedCallback_();
        sessionRecord_.reset();
    }
    updateReadiness();
    if (runningChangedCallback_) runningChangedCallback_();
}

auto OptimizationWorkspace::writeCandidateFiles(Candidate& candidate,
    const std::string& generatedDeck) -> bool
{
    const QDir directory(candidate.record.directory);
    const auto explicitMode = selectedFrequencyMode() == FrequencyMode::Explicit;
    const auto numericDeck = explicitMode
        ? analysis::prepareExplicitFrequencyInput(generatedDeck, activeFrequenciesMHz_)
        : analysis::prepareImpedanceInput(generatedDeck);
    const auto metadata = QJsonObject{
        {QStringLiteral("version"), 2},
        {QStringLiteral("variable"), selectedSymbol_},
        {QStringLiteral("value"), candidate.value},
        {QStringLiteral("unit"), selectedValueSuffix_.trimmed()},
        {QStringLiteral("objective"), activeObjective_.kind
            == analysis::OptimizationObjectiveKind::MaximumSwr
                ? QStringLiteral("minimize-maximum-swr")
                : QStringLiteral("minimize-swr-at-frequency")},
        {QStringLiteral("referenceImpedance"), activeObjective_.referenceImpedance},
        {QStringLiteral("targetFrequencyMHz"), activeObjective_.targetFrequencyMHz},
        {QStringLiteral("frequencyMode"), explicitMode
            ? QStringLiteral("explicit") : QStringLiteral("model-fr")},
        {QStringLiteral("frequenciesMHz"), frequencyArray(activeFrequenciesMHz_)},
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

auto OptimizationWorkspace::selectedObjective() const -> analysis::OptimizationObjectiveSpec
{
    return {
        .kind = static_cast<analysis::OptimizationObjectiveKind>(
            objectiveControl_->currentData().toInt()),
        .referenceImpedance = referenceImpedanceControl_->value(),
        .targetFrequencyMHz = targetFrequencyControl_->value(),
    };
}

auto OptimizationWorkspace::selectedFrequencyMode() const -> FrequencyMode
{
    return static_cast<FrequencyMode>(frequencyModeControl_->currentData().toInt());
}

auto OptimizationWorkspace::explicitFrequencies() const -> std::vector<double>
{
    std::vector<double> frequencies;
    frequencies.reserve(static_cast<std::size_t>(frequencyTable_->rowCount()));
    for (auto row = 0; row < frequencyTable_->rowCount(); ++row) {
        const auto* item = frequencyTable_->item(row, 0);
        if (item == nullptr) continue;
        bool valid{};
        const auto frequencyMHz = item->text().toDouble(&valid);
        if (valid && std::isfinite(frequencyMHz) && frequencyMHz > 0.0)
            frequencies.push_back(frequencyMHz);
    }
    std::ranges::sort(frequencies);
    const auto duplicates = std::ranges::unique(frequencies);
    frequencies.erase(duplicates.begin(), duplicates.end());
    return frequencies;
}

auto OptimizationWorkspace::objectiveName(analysis::OptimizationObjectiveKind kind) const -> QString
{
    return kind == analysis::OptimizationObjectiveKind::MaximumSwr
        ? tr("Worst SWR")
        : tr("SWR at Selected Frequency");
}

}
