#include "ui/DetachablePanel.h"

#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>
#include <algorithm>
#include <ranges>

namespace necwb::ui {

DetachablePanel::DetachablePanel(QString id, QString title, QWidget* parent,
    QString settingsRoot, QString windowObjectName)
    : QWidget(parent), id_(std::move(id)), title_(std::move(title)),
      settingsRoot_(std::move(settingsRoot)), windowObjectName_(std::move(windowObjectName))
{
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);

    auto* toolbar = new QHBoxLayout;
    statusText_ = tr("No results");
    statusLabel_ = new QLabel(statusText_, this);
    statusLabel_->setObjectName(id_ + QStringLiteral("StatusLabel"));
    toolbar->addWidget(statusLabel_);
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
    saveWindowState();
    delete dialog_;
}

void DetachablePanel::setContent(QWidget* content)
{
    if (content_ != nullptr || content == nullptr) return;
    content_ = content;
    content_->setParent(this);
    layout_->addWidget(content_, 1);
    QTimer::singleShot(0, this, [this] {
        const QSettings settings;
        const auto prefix = settingsKey({});
        if (settings.value(prefix + QStringLiteral("detached"), false).toBool())
            detach(settings.value(prefix + QStringLiteral("visible"), false).toBool());
    });
}

auto DetachablePanel::isDetached() const -> bool
{
    return detached_;
}

auto DetachablePanel::isWindowVisible() const -> bool
{
    return detached_ && dialog_ != nullptr && dialog_->isVisible();
}

auto DetachablePanel::isWindowActive() const -> bool
{
    return detached_ && dialog_ != nullptr && dialog_->isActiveWindow();
}

void DetachablePanel::showWindow(WindowActivation activation)
{
    if (detached_) showDetached(activation);
    else {
        detach(false);
        showDetached(activation);
    }
}

void DetachablePanel::setStatusText(QString status)
{
    statusText_ = std::move(status);
    statusLabel_->setText(statusText_);
    if (dialogStatusLabel_ != nullptr) dialogStatusLabel_->setText(statusText_);
}

void DetachablePanel::setVisibilityChangedCallback(std::function<void(bool)> callback)
{
    visibilityChanged_ = std::move(callback);
    if (visibilityChanged_) visibilityChanged_(isWindowVisible());
}

void DetachablePanel::toggleDetached()
{
    if (detached_) attach();
    else detach();
}

void DetachablePanel::detach(bool showWindow)
{
    if (detached_ || content_ == nullptr) return;
    if (dialog_ == nullptr) {
        dialog_ = new QDialog(nullptr, Qt::Window);
        dialog_->setObjectName(windowObjectName_.isEmpty()
            ? id_ + QStringLiteral("ResultWindow") : windowObjectName_);
        dialog_->setWindowTitle(tr("%1 — NEC Workbench").arg(title_));
        dialog_->setModal(false);
        dialog_->setAttribute(Qt::WA_QuitOnClose, false);
        dialog_->setWindowFlag(Qt::WindowMinimizeButtonHint, true);
        dialog_->setWindowFlag(Qt::WindowMaximizeButtonHint, true);
        auto* dialogLayout = new QVBoxLayout(dialog_);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        dialogLayout->setSizeConstraint(QLayout::SetNoConstraint);
        dialogStatusLabel_ = new QLabel(statusText_, dialog_);
        dialogStatusLabel_->setObjectName(id_ + QStringLiteral("WindowStatusLabel"));
        dialogStatusLabel_->setContentsMargins(6, 4, 6, 0);
        dialogLayout->addWidget(dialogStatusLabel_);
        connect(dialog_, &QDialog::finished, this, [this] {
            saveWindowGeometry();
            saveWindowState();
            if (visibilityChanged_) visibilityChanged_(false);
        });
    }

    layout_->removeWidget(content_);
    content_->setParent(dialog_);
    dialog_->layout()->addWidget(content_);
    content_->show();
    dialog_->layout()->invalidate();
    dialog_->layout()->activate();
    if (!windowGeometryInitialized_) {
        const auto geometry = QSettings{}.value(
            settingsKey(QStringLiteral("geometry"))).toByteArray();
        if (!geometry.isEmpty()) dialog_->restoreGeometry(geometry);
        else dialog_->resize(900, 680);
        windowGeometryInitialized_ = true;
    }
    ensureWindowOnScreen();
    placeholder_->show();
    detached_ = true;
    toggleButton_->setText(tr("Attach"));
    saveWindowState();
    if (showWindow) showDetached();
    else dialog_->hide();
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
    saveWindowState();
    if (visibilityChanged_) visibilityChanged_(false);
}

void DetachablePanel::showDetached(WindowActivation activation)
{
    if (dialog_ == nullptr) return;
    ensureWindowOnScreen();
    if (dialog_->isMinimized()) dialog_->showNormal();
    else dialog_->show();
    if (activation == WindowActivation::Activate) {
        dialog_->raise();
        dialog_->activateWindow();
    }
    saveWindowState();
    if (visibilityChanged_) visibilityChanged_(true);
}

void DetachablePanel::ensureWindowOnScreen()
{
    if (dialog_ == nullptr) return;
    const auto screens = QGuiApplication::screens();
    if (screens.empty()) return;

    const auto geometry = dialog_->frameGeometry();
    const QScreen* screen = nullptr;
    auto visibleArea = 0;
    for (const auto* candidate : screens) {
        const auto intersection = geometry.intersected(candidate->availableGeometry());
        const auto area = intersection.width() * intersection.height();
        if (area > visibleArea) {
            screen = candidate;
            visibleArea = area;
        }
    }
    if (screen == nullptr) screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) return;

    const auto available = screen->availableGeometry();
    auto size = dialog_->size().boundedTo(available.size());
    auto position = dialog_->pos();
    if (visibleArea < 100 * 60) {
        position = available.center() - QPoint(size.width() / 2, size.height() / 2);
    } else {
        position.setX(std::clamp(position.x(), available.left(),
            available.right() - size.width() + 1));
        position.setY(std::clamp(position.y(), available.top(),
            available.bottom() - size.height() + 1));
    }
    dialog_->resize(size);
    dialog_->move(position);
}

void DetachablePanel::saveWindowGeometry() const
{
    if (dialog_ != nullptr)
        QSettings{}.setValue(settingsKey(QStringLiteral("geometry")), dialog_->saveGeometry());
}

void DetachablePanel::saveWindowState() const
{
    const auto visible = isWindowVisible();
    if (savedWindowState_ && savedDetached_ == detached_ && savedVisible_ == visible) return;

    const auto prefix = settingsKey({});
    QSettings settings;
    settings.setValue(prefix + QStringLiteral("detached"), detached_);
    settings.setValue(prefix + QStringLiteral("visible"), visible);
    savedWindowState_ = true;
    savedDetached_ = detached_;
    savedVisible_ = visible;
}

auto DetachablePanel::settingsKey(const QString& name) const -> QString
{
    return QStringLiteral("%1/%2/%3").arg(settingsRoot_, id_, name);
}

}
