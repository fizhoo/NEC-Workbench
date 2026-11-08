#include "ui/bottom/RunLogView.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace necwb::ui {

RunLogView::RunLogView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("runLogView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(4, 0, 0, 0);
    header->setSpacing(4);
    activity_ = new QLabel(tr("Idle · no solver run has started"), this);
    activity_->setObjectName(QStringLiteral("runLogActivity"));
    activity_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("runLogProgress"));
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setFixedSize(80, 8);
    auto* copyCommand = new QToolButton(this);
    copyCommand->setText(tr("Copy Command"));
    copyCommand->setAutoRaise(true);
    auto* openFolder = new QToolButton(this);
    openFolder->setText(tr("Open Run Folder"));
    openFolder->setAutoRaise(true);
    stop_ = new QToolButton(this);
    stop_->setObjectName(QStringLiteral("runLogStopButton"));
    stop_->setText(tr("Stop"));
    stop_->setAutoRaise(true);
    stop_->setEnabled(false);
    header->addWidget(activity_, 1);
    header->addWidget(progress_);
    header->addWidget(copyCommand);
    header->addWidget(openFolder);
    header->addWidget(stop_);
    log_ = new QPlainTextEdit(this);
    log_->setObjectName(QStringLiteral("runLog"));
    log_->setReadOnly(true);
    log_->setFrameShape(QFrame::NoFrame);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setPlaceholderText(tr(
        "External solver command, progress, and diagnostics will appear here."));
    layout->addLayout(header);
    layout->addWidget(log_, 1);

    connect(copyCommand, &QToolButton::clicked, this, [this] {
        if (!command_.isEmpty()) QApplication::clipboard()->setText(command_);
    });
    connect(openFolder, &QToolButton::clicked, this, [this] {
        if (!runDirectory_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(runDirectory_));
    });
    connect(stop_, &QToolButton::clicked, this, &RunLogView::stopRequested);
}

void RunLogView::beginRun(const QString& context, const QString& command,
    const QString& runDirectory)
{
    command_ = command;
    runDirectory_ = runDirectory;
    log_->clear();
    activity_->setToolTip(tr("%1\nRun folder: %2\nCommand: %3")
        .arg(context, runDirectory, command));
    progress_->setRange(0, 0);
    stop_->setEnabled(true);
}

void RunLogView::append(const QString& text)
{
    log_->moveCursor(QTextCursor::End);
    log_->insertPlainText(text);
    log_->moveCursor(QTextCursor::End);
}

void RunLogView::setActivity(const QString& activity)
{
    activity_->setText(activity);
}

void RunLogView::setStopEnabled(bool enabled)
{
    stop_->setEnabled(enabled);
}

void RunLogView::finishRun(bool completed)
{
    progress_->setRange(0, 1);
    progress_->setValue(completed ? 1 : 0);
    stop_->setEnabled(false);
}

void RunLogView::showArchivedLog(const QString& text)
{
    log_->setPlainText(text);
}

}
