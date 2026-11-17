#include "ui/start/StartPanel.h"

#include "ui/start/RecentModels.h"

#include <QAction>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace necwb::ui {

StartPanel::StartPanel(QAction* newModel, QAction* open, std::function<void()> openExample,
    std::function<void(const QString&)> openRecent, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("startPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    const auto addButton = [&](const QString& text, const QString& name) {
        auto* button = new QPushButton(text, this);
        button->setObjectName(name);
        buttons->addWidget(button);
        return button;
    };
    connect(addButton(tr("New"), QStringLiteral("startNewModel")), &QPushButton::clicked,
        newModel, &QAction::trigger);
    connect(addButton(tr("Open…"), QStringLiteral("startOpenModel")), &QPushButton::clicked,
        open, &QAction::trigger);
    connect(addButton(tr("Open Example…"), QStringLiteral("startOpenExample")),
        &QPushButton::clicked, this, [openExample = std::move(openExample)] { openExample(); });
    buttons->addStretch();
    layout->addLayout(buttons);

    auto* heading = new QLabel(tr("Recent models"), this);
    auto headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);
    noRecent_ = new QLabel(tr("No recent models yet."), this);
    noRecent_->setObjectName(QStringLiteral("startNoRecentModels"));
    layout->addWidget(noRecent_);
    recent_ = new QTreeWidget(this);
    recent_->setObjectName(QStringLiteral("startRecentModels"));
    recent_->setHeaderLabels({tr("File"), tr("Folder"), tr("Opened")});
    recent_->setRootIsDecorated(false);
    recent_->setUniformRowHeights(true);
    recent_->setAlternatingRowColors(true);
    recent_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    recent_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    recent_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    recent_->header()->setStretchLastSection(false);
    recent_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    recent_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    recent_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(recent_, &QTreeWidget::itemActivated, this,
        [openRecent = std::move(openRecent)](QTreeWidgetItem* item) {
            if (item != nullptr) openRecent(item->data(0, Qt::UserRole).toString());
        });
    layout->addWidget(recent_);
    layout->addStretch(1);
}

void StartPanel::setRecentFiles(const QStringList& paths)
{
    recent_->clear();
    const QLocale locale;
    for (const auto& path : paths) {
        const QFileInfo file(path);
        const auto opened = recentModels::openedAt(path);
        auto* item = new QTreeWidgetItem(recent_, {file.fileName(),
            QDir::toNativeSeparators(file.absolutePath()),
            opened.isValid() ? locale.toString(opened, QLocale::ShortFormat) : QString{}});
        item->setData(0, Qt::UserRole, path);
        item->setToolTip(0, path);
        item->setToolTip(1, path);
    }
    noRecent_->setVisible(paths.isEmpty());
    recent_->setVisible(!paths.isEmpty());
    fitRecentList();
}

void StartPanel::fitRecentList()
{
    // As tall as its rows, so the list never fills the center: a scroll area's minimum
    // size hint would otherwise hold it taller.
    recent_->setMaximumHeight(recent_->sizeHint().height());
}

void StartPanel::showEvent(QShowEvent* event)
{
    fitRecentList();
    QWidget::showEvent(event);
}

void StartPanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        fitRecentList();
    QWidget::changeEvent(event);
}

}
