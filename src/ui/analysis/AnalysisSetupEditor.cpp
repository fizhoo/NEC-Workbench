#include "ui/analysis/AnalysisSetupEditor.h"

#include "analysis/SolverCommand.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

namespace necwb::ui {

AnalysisSetupEditor::AnalysisSetupEditor(QWidget* parent)
    : QWidget(parent)
{
    // A short form that fits the left column; how each engine is run is in the tooltips.
    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    backendControl_ = new QComboBox(this);
    backendControl_->setObjectName(QStringLiteral("analysisBackendControl"));
    backendControl_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    backendControl_->setMinimumContentsLength(8);
    for (const auto& [name, id] : backends()) backendControl_->addItem(name, id);
    auto* executableRow = new QWidget(this);
    auto* executableLayout = new QHBoxLayout(executableRow);
    executableLayout->setContentsMargins(0, 0, 0, 0);
    executableControl_ = new QLineEdit(executableRow);
    executableControl_->setObjectName(QStringLiteral("analysisExecutableControl"));
    executableControl_->setPlaceholderText(tr("Solver executable"));
    executableControl_->setToolTip(tr("The executable stays external; Workbench never bundles a solver"));
    auto* browseButton = new QPushButton(tr("Browse…"), executableRow);
    executableLayout->addWidget(executableControl_, 1);
    executableLayout->addWidget(browseButton);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    timeoutControl_ = new QSpinBox(this);
    timeoutControl_->setRange(5, 86400);
    timeoutControl_->setSuffix(tr(" s"));
    timeoutControl_->setValue(120);
    timeoutControl_->setToolTip(tr("Stop a run that takes longer than this"));
    layout->addRow(tr("Engine"), backendControl_);
    layout->addRow(tr("Path"), executableRow);
    layout->addRow(tr("Timeout"), timeoutControl_);
    layout->addRow(tr("Status"), status_);

    connect(backendControl_, &QComboBox::currentIndexChanged, this, [this] {
        if (!updating_) {
            if (!selectedBackendId_.isEmpty()) {
                executablePaths_.insert(selectedBackendId_, executableControl_->text());
                emit executablePathChanged(selectedBackendId_, executableControl_->text());
            }
            selectedBackendId_ = backendControl_->currentData().toString();
            executableControl_->setText(executablePaths_.value(selectedBackendId_));
        }
        updateDescription();
        emitSettings();
    });
    connect(executableControl_, &QLineEdit::editingFinished, this, [this] {
        executablePaths_.insert(selectedBackendId_, executableControl_->text());
        emit executablePathChanged(selectedBackendId_, executableControl_->text());
        updateDescription();
        emitSettings();
    });
    connect(timeoutControl_, &QSpinBox::valueChanged, this, [this] { emitSettings(); });
    connect(browseButton, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Select NEC Solver Executable"),
            executableControl_->text());
        if (!path.isEmpty()) {
            executableControl_->setText(path);
            executablePaths_.insert(selectedBackendId_, path);
            emit executablePathChanged(selectedBackendId_, path);
            updateDescription();
            emitSettings();
        }
    });
    updateDescription();
}

auto AnalysisSetupEditor::backends() -> std::vector<std::pair<QString, QString>>
{
    return {{tr("NEC-2 / nec2c"), QStringLiteral("nec2")},
        {tr("OpenNEC"), QStringLiteral("opennec")},
        {tr("4nec2 NEC2dXS"), QStringLiteral("nec2dxs")}};
}

void AnalysisSetupEditor::selectBackend(const QString& backendId)
{
    const auto index = backendControl_->findData(backendId);
    if (index >= 0) backendControl_->setCurrentIndex(index);
}

void AnalysisSetupEditor::setSettings(const QString& backendId,
    const QHash<QString, QString>& executablePaths, int timeoutSeconds)
{
    updating_ = true;
    executablePaths_ = executablePaths;
    const auto index = backendControl_->findData(backendId);
    backendControl_->setCurrentIndex(index >= 0 ? index : 0);
    selectedBackendId_ = backendControl_->currentData().toString();
    executableControl_->setText(executablePaths_.value(selectedBackendId_));
    timeoutControl_->setValue(timeoutSeconds);
    updating_ = false;
    updateDescription();
}

void AnalysisSetupEditor::updateDescription()
{
    const auto backend = backendControl_->currentData().toString();
    if (backend == QStringLiteral("opennec")) {
        backendControl_->setToolTip(tr(
            "OpenNEC receives the input deck as a positional argument and writes original NEC-2 report output."));
    } else if (backend == QStringLiteral("nec2dxs")) {
        backendControl_->setToolTip(tr(
            "4nec2 NEC2dXS receives the input and output filenames through standard input. Browse to any desired NEC2dXS capacity executable."));
    } else {
        backendControl_->setToolTip(tr(
            "nec2c receives working-directory-relative input and output paths through -i and -o arguments."));
    }
    const QFileInfo executable(executableControl_->text());
    if (executableControl_->text().isEmpty()) {
        status_->setText(tr("No executable selected."));
    } else if (!executable.exists() || !executable.isFile()) {
        status_->setText(tr("Executable path does not exist."));
    } else if (!executable.isExecutable()) {
        status_->setText(tr("The selected file is not executable."));
    } else {
        status_->setText(tr("Executable found. This backend is ready to run."));
        if (const auto capacity = analysis::solverSegmentCapacity(
                backend.toStdString(), executable.absoluteFilePath().toStdString())) {
            status_->setText(tr("Executable found · maximum %1 segments.").arg(*capacity));
        }
    }
}

void AnalysisSetupEditor::emitSettings()
{
    if (!updating_) {
        emit settingsChanged(backendControl_->currentData().toString(), executableControl_->text(),
            timeoutControl_->value());
    }
}

}
