#include "ui/WorkbenchStatusBar.h"

#include "ui/DisplayFormat.h"

#include <QLabel>
#include <QToolButton>

#include <algorithm>

namespace necwb::ui {

WorkbenchStatusBar::WorkbenchStatusBar(QWidget* parent)
    : QStatusBar(parent)
{
    setObjectName(QStringLiteral("workbenchStatusBar"));
    setSizeGripEnabled(false);
    file_ = new QLabel(this);
    file_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    file_->setMinimumWidth(0);
    file_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    backend_ = new QLabel(this);
    backend_->setObjectName(QStringLiteral("statusBackend"));
    backend_->setToolTip(tr("Solver backend"));
    frequency_ = new QLabel(tr("No results"), this);
    frequency_->setObjectName(QStringLiteral("statusResultFrequency"));
    frequency_->setToolTip(tr("Frequencies of the loaded results"));
    runState_ = new QLabel(tr("Idle"), this);
    runState_->setObjectName(QStringLiteral("statusRunState"));
    counts_ = new QToolButton(this);
    counts_->setObjectName(QStringLiteral("statusCheckCounts"));
    counts_->setAutoRaise(true);
    counts_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addPermanentWidget(file_, 1);
    addPermanentWidget(backend_);
    addPermanentWidget(frequency_);
    addPermanentWidget(runState_);
    addPermanentWidget(counts_);
    setCheckRequired();
    connect(counts_, &QToolButton::clicked, this, &WorkbenchStatusBar::problemsRequested);
}

void WorkbenchStatusBar::setFileStatus(const QString& text, const QString& toolTip)
{
    file_->setText(text);
    file_->setToolTip(toolTip);
}

void WorkbenchStatusBar::setBackend(const QString& backend)
{
    backend_->setText(backend);
}

void WorkbenchStatusBar::setResultFrequencies(const std::vector<double>& frequenciesMHz)
{
    if (frequenciesMHz.empty()) {
        frequency_->setText(tr("No results"));
        return;
    }
    const auto [low, high] = std::ranges::minmax(frequenciesMHz);
    frequency_->setText(frequenciesMHz.size() == 1
            ? tr("%1 MHz").arg(formatDecimal(low))
            : tr("%1–%2 MHz · %3 pts").arg(formatDecimal(low), formatDecimal(high))
                  .arg(frequenciesMHz.size()));
}

void WorkbenchStatusBar::setRunning(double elapsedSeconds)
{
    runState_->setText(tr("Running · %1 s").arg(elapsedSeconds, 0, 'f', 1));
}

void WorkbenchStatusBar::setRunFinished(const QString& status, bool succeeded,
    double elapsedSeconds)
{
    // Unsuccessful statuses already say what happened: "Failed (exit 1)", "Timed out", "Canceled".
    runState_->setText(succeeded ? tr("Done · %1 s").arg(elapsedSeconds, 0, 'f', 1) : status);
}

void WorkbenchStatusBar::setCheckCounts(std::size_t errors, std::size_t warnings,
    const QString& detail)
{
    counts_->setText(tr("%1 E · %2 W").arg(errors).arg(warnings));
    counts_->setToolTip(tr("%1\nClick to show Problems").arg(detail));
}

void WorkbenchStatusBar::setCheckRequired()
{
    counts_->setText(tr("— E · — W"));
    counts_->setToolTip(tr("Model changed — check required\nClick to show Problems"));
}

}
