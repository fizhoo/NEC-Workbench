#include "ui/bottom/RunHistoryView.h"

#include "ui/DisplayFormat.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace necwb::ui {
namespace {

constexpr auto RunDirectoryRole = Qt::UserRole + 3;
constexpr auto RunContextRole = Qt::UserRole + 4;
constexpr auto RunTypeRole = Qt::UserRole + 6;
constexpr auto RunIdRole = Qt::UserRole + 7;

enum RunColumn {
    RunStartedColumn,
    RunModelColumn,
    RunNoteColumn,
    RunTypeColumn,
    RunResultsColumn,
    RunOutputSizeColumn,
    RunBackendColumn,
    RunStatusColumn,
    RunDurationColumn,
    RunFolderColumn,
    RunColumnCount
};

auto runResultsText(const AnalysisRunRecord& record) -> QString
{
    if (record.runType == QStringLiteral("optimization-session"))
        return record.summary.isEmpty()
            ? QObject::tr("%1 candidates").arg(record.candidateCount) : record.summary;
    if (record.runType == QStringLiteral("convergence-session"))
        return record.summary.isEmpty()
            ? QObject::tr("%1 levels").arg(record.candidateCount) : record.summary;
    if (record.runType == QStringLiteral("average-gain-test"))
        return record.summary.isEmpty() ? QObject::tr("AGT") : record.summary;
    QStringList types;
    if (record.hasImpedance) types.append(QObject::tr("Z"));
    if (record.hasCurrents) types.append(QObject::tr("I"));
    if (record.hasRadiation) types.append(QObject::tr("RP"));
    if (types.empty()) return record.outputBytes > 0 ? QObject::tr("Not indexed") : QObject::tr("—");
    return record.frequencyCount > 0
        ? QObject::tr("%1 freq · %2").arg(record.frequencyCount).arg(types.join(QStringLiteral(" · ")))
        : types.join(QStringLiteral(" · "));
}

auto toolButton(const QString& objectName, const QString& text, QWidget* parent) -> QToolButton*
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(text);
    button->setAutoRaise(true);
    button->setEnabled(false);
    return button;
}

}

auto formatByteSize(qint64 bytes) -> QString
{
    if (bytes <= 0) return QObject::tr("—");
    constexpr auto kibibyte = 1024.0;
    constexpr auto mebibyte = kibibyte * 1024.0;
    if (bytes >= mebibyte) return QObject::tr("%1 MB").arg(formatDecimal(bytes / mebibyte));
    if (bytes >= kibibyte) return QObject::tr("%1 KB").arg(formatDecimal(bytes / kibibyte));
    return QObject::tr("%1 B").arg(bytes);
}

auto runContext(const AnalysisRunRecord& record) -> QString
{
    const auto modelName = record.sourceFile.isEmpty()
        ? QObject::tr("Archived model.nec")
        : QFileInfo(record.sourceFile).fileName();
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

RunHistoryView::RunHistoryView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("runHistoryView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(0);
    review_ = toolButton(QStringLiteral("openRunResultsButton"), tr("Review"), this);
    review_->setToolTip(tr("Inspect the run in the separate Run Review window"));
    inspectInput_ = toolButton(QStringLiteral("inspectRunInputButton"), tr("Input…"), this);
    inspectInput_->setToolTip(tr("Show the read-only NEC deck this run used"));
    openSnapshot_ = toolButton(QStringLiteral("openRunSnapshotButton"), tr("Open as New Model"), this);
    openSnapshot_->setToolTip(tr("Replace the active model with an editable copy of this run's input"));
    overlay_ = toolButton(QStringLiteral("pinRunReferenceButton"), tr("Use as Overlay"), this);
    overlay_->setToolTip(tr("Use this run as the historical overlay in the results column"));
    editNote_ = toolButton(QStringLiteral("editRunNoteButton"), tr("Note…"), this);
    openFolder_ = toolButton(QStringLiteral("openRunFolderButton"), tr("Folder"), this);
    openFolder_->setToolTip(tr("Open the run folder"));
    delete_ = toolButton(QStringLiteral("deleteRunButton"), tr("Delete…"), this);
    for (auto* button : {review_, inspectInput_, openSnapshot_, overlay_, editNote_, openFolder_})
        buttons->addWidget(button);
    buttons->addStretch();
    buttons->addWidget(delete_);
    table_ = new QTableWidget(0, RunColumnCount, this);
    table_->setObjectName(QStringLiteral("analysisRunsTable"));
    table_->setHorizontalHeaderLabels({tr("Started"), tr("Model"), tr("Note"), tr("Type"),
        tr("Results"), tr("Output"), tr("Backend"), tr("Status"), tr("Duration"), tr("Run Folder")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setFrameShape(QFrame::NoFrame);
    table_->setWordWrap(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(table_->fontMetrics().height() + 4);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addLayout(buttons);
    layout->addWidget(table_, 1);

    connect(review_, &QToolButton::clicked, this, &RunHistoryView::reviewRequested);
    connect(inspectInput_, &QToolButton::clicked, this, &RunHistoryView::inspectInputRequested);
    connect(openSnapshot_, &QToolButton::clicked, this, &RunHistoryView::openSnapshotRequested);
    connect(overlay_, &QToolButton::clicked, this, &RunHistoryView::overlayRequested);
    connect(editNote_, &QToolButton::clicked, this, &RunHistoryView::editNoteRequested);
    connect(delete_, &QToolButton::clicked, this, &RunHistoryView::deleteRequested);
    connect(openFolder_, &QToolButton::clicked, this, [this] {
        if (const auto selected = selection())
            QDesktopServices::openUrl(QUrl::fromLocalFile(selected->record.directory));
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateActions(); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int, int column) {
        if (column == RunNoteColumn) emit editNoteRequested();
        else if (review_->isEnabled()) emit reviewRequested();
    });
}

void RunHistoryView::setRecordSource(std::function<std::vector<AnalysisRunRecord>()> source)
{
    source_ = std::move(source);
}

void RunHistoryView::reload()
{
    if (loaded_) load();
}

void RunHistoryView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!loaded_) load();
}

void RunHistoryView::load()
{
    if (!source_) return;
    table_->setRowCount(0);
    table_->setUpdatesEnabled(false);
    for (const auto& record : source_()) {
        if (record.runType == QStringLiteral("optimization-candidate")
            || record.runType == QStringLiteral("convergence-step")) continue;
        const auto row = table_->rowCount();
        table_->insertRow(row);
        setRow(row, record);
    }
    table_->setUpdatesEnabled(true);
    fitColumns();
    table_->clearSelection();
    loaded_ = true;
    updateActions();
}

void RunHistoryView::addActiveRun(const AnalysisRunRecord& record)
{
    table_->insertRow(0);
    setRow(0, record);
    fitColumns();
    table_->selectRow(0);
}

void RunHistoryView::updateRun(const AnalysisRunRecord& record)
{
    const auto row = rowForDirectory(record.directory);
    if (row >= 0) setRow(row, record);
    updateActions();
}

void RunHistoryView::updateRunSummary(const QString& directory, const AnalysisRunRecord& summary)
{
    const auto row = rowForDirectory(directory);
    if (row < 0) return;
    table_->item(row, RunResultsColumn)->setText(runResultsText(summary));
    table_->item(row, RunOutputSizeColumn)->setText(formatByteSize(summary.outputBytes));
}

void RunHistoryView::removeRun(const QString& directory)
{
    const auto row = rowForDirectory(directory);
    if (row >= 0) table_->removeRow(row);
    updateActions();
}

void RunHistoryView::setBusy(bool solverBusy, const QString& activeDirectory)
{
    solverBusy_ = solverBusy;
    activeDirectory_ = activeDirectory;
    updateActions();
}

auto RunHistoryView::selection() const -> std::optional<Selection>
{
    const auto row = table_->currentRow();
    const auto* item = row >= 0 ? table_->item(row, RunStartedColumn) : nullptr;
    if (item == nullptr) return std::nullopt;
    Selection selected;
    selected.record.id = item->data(RunIdRole).toString();
    selected.record.directory = item->data(RunDirectoryRole).toString();
    selected.record.runType = item->data(RunTypeRole).toString();
    selected.record.started = QDateTime::fromString(item->text(),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    selected.record.backend = table_->item(row, RunBackendColumn)->text();
    selected.record.status = table_->item(row, RunStatusColumn)->text();
    selected.modelName = table_->item(row, RunModelColumn)->text();
    selected.record.sourceFile = selected.modelName;
    selected.context = item->data(RunContextRole).toString();
    return selected;
}

void RunHistoryView::setRow(int row, const AnalysisRunRecord& record)
{
    const auto outputBytes = record.outputBytes > 0 ? record.outputBytes
        : QFileInfo(QDir(record.directory).filePath(QStringLiteral("model.out"))).size();
    auto displayRecord = record;
    displayRecord.outputBytes = outputBytes;
    const auto type = record.runType == QStringLiteral("optimization-session")
        ? tr("Optimization")
        : record.runType == QStringLiteral("convergence-session") ? tr("Convergence")
        : record.runType == QStringLiteral("average-gain-test") ? tr("AGT")
        : record.runType == QStringLiteral("quick-sweep") ? tr("Quick Sweep") : tr("Analysis");
    const QStringList values{
        record.started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        record.sourceFile.isEmpty() ? tr("Archived model.nec") : QFileInfo(record.sourceFile).fileName(),
        record.userNote.trimmed().isEmpty() ? tr("—") : record.userNote.simplified(),
        type,
        runResultsText(displayRecord),
        formatByteSize(outputBytes),
        record.backend,
        record.status,
        record.durationSeconds > 0.0
            ? tr("%1 s").arg(formatDecimal(record.durationSeconds)) : tr("—"),
        record.directory,
    };
    for (auto column = 0; column < values.size(); ++column) {
        auto* item = new QTableWidgetItem(values[column]);
        item->setData(RunDirectoryRole, record.directory);
        item->setData(RunContextRole, runContext(record));
        item->setData(RunTypeRole, record.runType);
        item->setData(RunIdRole, record.id);
        if (column == RunNoteColumn && !record.userNote.trimmed().isEmpty())
            item->setToolTip(record.userNote);
        table_->setItem(row, column, item);
    }
}

auto RunHistoryView::rowForDirectory(const QString& directory) const -> int
{
    for (auto row = 0; row < table_->rowCount(); ++row) {
        const auto* item = table_->item(row, RunStartedColumn);
        if (item != nullptr && item->data(RunDirectoryRole).toString() == directory) return row;
    }
    return -1;
}

void RunHistoryView::fitColumns()
{
    table_->resizeColumnsToContents();
    table_->setColumnWidth(RunNoteColumn,
        std::clamp(table_->columnWidth(RunNoteColumn), 120, 240));
}

void RunHistoryView::updateActions()
{
    const auto selected = selection();
    const auto hasSelection = selected.has_value();
    const auto directory = hasSelection ? selected->record.directory : QString{};
    const auto activeSelection = hasSelection && !activeDirectory_.isEmpty()
        && directory == activeDirectory_;
    const auto selectedType = hasSelection ? selected->record.runType : QString{};
    review_->setEnabled(hasSelection && !activeSelection && !solverBusy_);
    review_->setText(selectedType == QStringLiteral("optimization-session")
        ? tr("Review Optimization")
        : selectedType == QStringLiteral("convergence-session") ? tr("Review Convergence")
        : selectedType == QStringLiteral("average-gain-test") ? tr("Review AGT") : tr("Review"));
    const auto hasInputSnapshot = hasSelection
        && QFileInfo::exists(QDir(directory).filePath(QStringLiteral("model.nec")));
    const auto isSession = selectedType == QStringLiteral("optimization-session")
        || selectedType == QStringLiteral("convergence-session");
    inspectInput_->setEnabled(hasInputSnapshot && !isSession);
    openSnapshot_->setEnabled(hasInputSnapshot && !isSession && !activeSelection && !solverBusy_);
    overlay_->setEnabled(hasSelection && !activeSelection && !solverBusy_
        && (selectedType == QStringLiteral("analysis")
            || selectedType == QStringLiteral("quick-sweep"))
        && selected->record.status == tr("Completed")
        && QFileInfo::exists(QDir(directory).filePath(QStringLiteral("model.out"))));
    editNote_->setEnabled(hasSelection && !solverBusy_);
    openFolder_->setEnabled(hasSelection);
    delete_->setEnabled(hasSelection && !activeSelection && !solverBusy_);
}

}
