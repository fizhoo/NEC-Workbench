#include "ui/DetachablePanel.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <utility>

namespace necwb::ui {

DetachablePanel::DetachablePanel(QString id, QString title, QWidget* parent)
    : QWidget(parent), id_(std::move(id)), title_(std::move(title))
{
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);

    auto* toolbar = new QHBoxLayout;
    toolbar->addStretch();
    toggleButton_ = new QPushButton(tr("Pop Out"), this);
    toggleButton_->setObjectName(id_ + QStringLiteral("PopOutButton"));
    toggleButton_->setToolTip(tr("Keep this live result view in its own reusable window"));
    toolbar->addWidget(toggleButton_);
    layout_->addLayout(toolbar);

    placeholder_ = new QWidget(this);
    auto* placeholderLayout = new QVBoxLayout(placeholder_);
    placeholderLayout->setAlignment(Qt::AlignCenter);
    auto* message = new QLabel(tr("%1 is open in a separate window.").arg(title_), placeholder_);
    auto* showButton = new QPushButton(tr("Show Window"), placeholder_);
    auto* attachButton = new QPushButton(tr("Attach Here"), placeholder_);
    placeholderLayout->addWidget(message, 0, Qt::AlignCenter);
    placeholderLayout->addWidget(showButton, 0, Qt::AlignCenter);
    placeholderLayout->addWidget(attachButton, 0, Qt::AlignCenter);
    placeholder_->hide();
    layout_->addWidget(placeholder_, 1);

    connect(toggleButton_, &QPushButton::clicked, this, [this] { toggleDetached(); });
    connect(showButton, &QPushButton::clicked, this, [this] { showDetached(); });
    connect(attachButton, &QPushButton::clicked, this, [this] { attach(); });
}

DetachablePanel::~DetachablePanel()
{
    saveWindowGeometry();
}

void DetachablePanel::setContent(QWidget* content)
{
    if (content_ != nullptr || content == nullptr) return;
    content_ = content;
    content_->setParent(this);
    layout_->addWidget(content_, 1);
}

auto DetachablePanel::isDetached() const -> bool
{
    return detached_;
}

void DetachablePanel::toggleDetached()
{
    if (detached_) attach();
    else detach();
}

void DetachablePanel::detach()
{
    if (detached_ || content_ == nullptr) return;
    if (dialog_ == nullptr) {
        dialog_ = new QDialog(this, Qt::Window);
        dialog_->setObjectName(id_ + QStringLiteral("ResultWindow"));
        dialog_->setWindowTitle(tr("%1 — NEC Workbench").arg(title_));
        dialog_->setModal(false);
        dialog_->setWindowFlag(Qt::WindowMinimizeButtonHint, true);
        dialog_->setWindowFlag(Qt::WindowMaximizeButtonHint, true);
        auto* dialogLayout = new QVBoxLayout(dialog_);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        dialogLayout->setSizeConstraint(QLayout::SetNoConstraint);
        const auto geometry = QSettings{}.value(
            QStringLiteral("resultWindows/%1/geometry").arg(id_)).toByteArray();
        if (!geometry.isEmpty()) dialog_->restoreGeometry(geometry);
        else dialog_->resize(900, 680);
        connect(dialog_, &QDialog::finished, this, [this] { attach(); });
    }

    layout_->removeWidget(content_);
    content_->setParent(dialog_);
    dialog_->layout()->addWidget(content_);
    placeholder_->show();
    detached_ = true;
    toggleButton_->setText(tr("Attach"));
    showDetached();
}

void DetachablePanel::attach()
{
    if (!detached_ || content_ == nullptr) return;
    saveWindowGeometry();
    dialog_->layout()->removeWidget(content_);
    content_->setParent(this);
    layout_->addWidget(content_, 1);
    placeholder_->hide();
    content_->show();
    detached_ = false;
    toggleButton_->setText(tr("Pop Out"));
    dialog_->hide();
}

void DetachablePanel::showDetached()
{
    if (dialog_ == nullptr) return;
    if (dialog_->isMinimized()) dialog_->showNormal();
    else dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
}

void DetachablePanel::saveWindowGeometry() const
{
    if (dialog_ != nullptr)
        QSettings{}.setValue(QStringLiteral("resultWindows/%1/geometry").arg(id_),
            dialog_->saveGeometry());
}

}
