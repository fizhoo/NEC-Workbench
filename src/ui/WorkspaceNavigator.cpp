#include "ui/WorkspaceNavigator.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace necwb::ui {
namespace {

constexpr auto ExpandedWidth = 168;
constexpr auto CollapsedWidth = 44;

auto collapsedSettingsKey(const QString& id) -> QString
{
    return QStringLiteral("workspaceNavigation/%1/collapsed").arg(id);
}

}

WorkspaceNavigator::WorkspaceNavigator(
    QTabWidget* pages, QString settingsId, QWidget* parent)
    : QWidget(parent), pages_(pages), settingsId_(std::move(settingsId))
{
    setObjectName(settingsId_ + QStringLiteral("WorkspaceNavigator"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* navigationPanel = new QWidget(this);
    navigationPanel->setObjectName(settingsId_ + QStringLiteral("WorkspaceNavigationPanel"));
    auto* navigationLayout = new QVBoxLayout(navigationPanel);
    navigationLayout->setContentsMargins(4, 4, 4, 4);
    navigationLayout->setSpacing(4);

    collapseButton_ = new QToolButton(navigationPanel);
    collapseButton_->setObjectName(settingsId_ + QStringLiteral("WorkspaceCollapseButton"));
    collapseButton_->setAutoRaise(true);
    collapseButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    navigationLayout->addWidget(collapseButton_);

    navigation_ = new QListWidget(navigationPanel);
    navigation_->setObjectName(settingsId_ + QStringLiteral("WorkspaceNavigation"));
    navigation_->setIconSize({20, 20});
    navigation_->setSpacing(1);
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setTextElideMode(Qt::ElideRight);
    navigationLayout->addWidget(navigation_, 1);

    layout->addWidget(navigationPanel);
    layout->addWidget(pages_, 1);
    pages_->tabBar()->hide();

    populate();
    connect(navigation_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < pages_->count()) pages_->setCurrentIndex(row);
    });
    connect(pages_, &QTabWidget::currentChanged, this, [this](int index) {
        const QSignalBlocker blocker(navigation_);
        navigation_->setCurrentRow(index);
    });
    connect(collapseButton_, &QToolButton::clicked,
        this, [this] { setCollapsed(!collapsed_); });

    setCollapsed(QSettings{}.value(collapsedSettingsKey(settingsId_), false).toBool());
}

auto WorkspaceNavigator::pages() const noexcept -> QTabWidget*
{
    return pages_;
}

auto WorkspaceNavigator::isCollapsed() const noexcept -> bool
{
    return collapsed_;
}

void WorkspaceNavigator::setCollapsed(bool collapsed)
{
    collapsed_ = collapsed;
    updatePresentation();
    QSettings{}.setValue(collapsedSettingsKey(settingsId_), collapsed_);
}

void WorkspaceNavigator::populate()
{
    navigation_->clear();
    for (auto index = 0; index < pages_->count(); ++index) {
        auto* item = new QListWidgetItem(pages_->tabIcon(index), pages_->tabText(index));
        item->setData(Qt::UserRole, pages_->tabText(index));
        item->setToolTip(pages_->tabToolTip(index).isEmpty()
                ? pages_->tabText(index) : pages_->tabToolTip(index));
        navigation_->addItem(item);
    }
    navigation_->setCurrentRow(pages_->currentIndex());
}

void WorkspaceNavigator::updatePresentation()
{
    auto* panel = navigation_->parentWidget();
    panel->setFixedWidth(collapsed_ ? CollapsedWidth : ExpandedWidth);
    collapseButton_->setArrowType(collapsed_ ? Qt::RightArrow : Qt::LeftArrow);
    collapseButton_->setText(collapsed_ ? QString{} : tr("Collapse"));
    collapseButton_->setToolTip(collapsed_ ? tr("Expand workspace navigation")
                                           : tr("Collapse workspace navigation"));
    for (auto index = 0; index < navigation_->count(); ++index) {
        auto* item = navigation_->item(index);
        item->setText(collapsed_ ? QString{} : item->data(Qt::UserRole).toString());
        item->setTextAlignment(collapsed_ ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
    }
}

}
