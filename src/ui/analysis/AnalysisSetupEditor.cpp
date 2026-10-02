#include "ui/analysis/AnalysisSetupEditor.h"

#include "analysis/SolverCommand.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace necwb::ui {

AnalysisSetupEditor::AnalysisSetupEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    auto* heading = new QLabel(tr("Analysis Backend"), this);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);
    description_ = new QLabel(this);
    description_->setWordWrap(true);

    auto* backendGroup = new QGroupBox(tr("NEC Engine"), this);
    auto* backendLayout = new QFormLayout(backendGroup);
    backendLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    backendControl_ = new QComboBox(backendGroup);
    backendControl_->setObjectName(QStringLiteral("analysisBackendControl"));
    backendControl_->addItem(tr("NEC-2 / nec2c"), QStringLiteral("nec2"));
    backendControl_->addItem(tr("OpenNEC"), QStringLiteral("opennec"));
    backendControl_->addItem(tr("4nec2 NEC2dXS"), QStringLiteral("nec2dxs"));
    auto* executableRow = new QWidget(backendGroup);
    auto* executableLayout = new QHBoxLayout(executableRow);
    executableLayout->setContentsMargins(0, 0, 0, 0);
    executableControl_ = new QLineEdit(executableRow);
    executableControl_->setObjectName(QStringLiteral("analysisExecutableControl"));
    executableControl_->setPlaceholderText(tr("Select the solver executable"));
    auto* browseButton = new QPushButton(tr("Browse…"), executableRow);
    executableLayout->addWidget(executableControl_, 1);
    executableLayout->addWidget(browseButton);
    status_ = new QLabel(backendGroup);
    status_->setWordWrap(true);
    timeoutControl_ = new QSpinBox(backendGroup);
    timeoutControl_->setRange(5, 86400);
    timeoutControl_->setSuffix(tr(" seconds"));
    timeoutControl_->setValue(120);
    backendLayout->addRow(tr("Backend"), backendControl_);
    backendLayout->addRow(tr("Executable"), executableRow);
    backendLayout->addRow(tr("Run timeout"), timeoutControl_);
    backendLayout->addRow(tr("Status"), status_);

    auto* note = new QLabel(tr("The backend selects the executable's input/output protocol. The executable remains external and is never bundled with NEC Workbench. Run decks and output files are preserved for inspection."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(heading);
    layout->addWidget(description_);
    layout->addWidget(backendGroup);
    layout->addWidget(note);
    layout->addStretch();

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
        description_->setText(tr(
            "OpenNEC receives the input deck as a positional argument and writes original NEC-2 report output."));
    } else if (backend == QStringLiteral("nec2dxs")) {
        description_->setText(tr(
            "4nec2 NEC2dXS receives the input and output filenames through standard input. Browse to any desired NEC2dXS capacity executable."));
    } else {
        description_->setText(tr(
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
