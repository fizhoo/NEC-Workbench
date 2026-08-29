#include "ui/welcome/WelcomePage.h"

#include <QAction>
#include <QCommandLinkButton>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace necwb::ui {
namespace {

auto createActionButton(QAction* action, const QString& body, QWidget* parent)
    -> QCommandLinkButton*
{
    auto* button = new QCommandLinkButton(action->text().remove(QLatin1Char('&')), body, parent);
    button->setMinimumHeight(82);
    QObject::connect(button, &QCommandLinkButton::clicked, action, &QAction::trigger);
    return button;
}

auto resourceButton(const QString& title, const QString& body, QWidget* parent)
    -> QCommandLinkButton*
{
    auto* button = new QCommandLinkButton(title, body, parent);
    button->setMinimumHeight(70);
    return button;
}

}

WelcomePage::WelcomePage(QAction* newModelAction, QAction* openAction, PathCallback openRecent,
    VoidCallback openExamples, VoidCallback clearRecent, QWidget* parent)
    : QWidget(parent)
    , openRecent_(std::move(openRecent))
{
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(48, 38, 48, 38);
    pageLayout->setSpacing(20);

    auto* title = new QLabel(tr("NEC Workbench"), this);
    auto titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 10);
    title->setFont(titleFont);

    auto* subtitle = new QLabel(
        tr("Create a new antenna model or continue working on an existing NEC design."), this);
    auto subtitleFont = subtitle->font();
    subtitleFont.setPointSize(subtitleFont.pointSize() + 2);
    subtitle->setFont(subtitleFont);

    auto* primaryActions = new QHBoxLayout;
    primaryActions->setSpacing(14);
    primaryActions->addWidget(createActionButton(newModelAction,
        tr("Start with a blank, editable NEC source deck."), this));
    primaryActions->addWidget(createActionButton(openAction,
        tr("Open a .nec file from this computer."), this));

    auto* recentGroup = new QGroupBox(tr("Recent Models"), this);
    auto* recentLayout = new QVBoxLayout(recentGroup);
    noRecentFiles_ = new QLabel(tr("No recent NEC models yet."), recentGroup);
    noRecentFiles_->setAlignment(Qt::AlignCenter);
    recentFiles_ = new QListWidget(recentGroup);
    recentFiles_->setObjectName(QStringLiteral("recentModelsList"));
    recentFiles_->setAlternatingRowColors(true);
    recentFiles_->setMinimumHeight(155);
    auto* recentButtons = new QHBoxLayout;
    auto* openSelected = new QPushButton(tr("Open Selected"), recentGroup);
    openSelected->setObjectName(QStringLiteral("openRecentModelButton"));
    auto* clearButton = new QPushButton(tr("Clear Recent List"), recentGroup);
    clearButton->setObjectName(QStringLiteral("clearRecentModelsButton"));
    recentButtons->addWidget(openSelected);
    recentButtons->addWidget(clearButton);
    recentButtons->addStretch();
    recentLayout->addWidget(noRecentFiles_);
    recentLayout->addWidget(recentFiles_, 1);
    recentLayout->addLayout(recentButtons);
    const auto openSelection = [this] {
        if (const auto* item = recentFiles_->currentItem(); item != nullptr && openRecent_)
            openRecent_(item->data(Qt::UserRole).toString());
    };
    connect(openSelected, &QPushButton::clicked, this, openSelection);
    connect(recentFiles_, &QListWidget::itemActivated, this,
        [this](QListWidgetItem* item) { if (item != nullptr && openRecent_) openRecent_(item->data(Qt::UserRole).toString()); });
    connect(clearButton, &QPushButton::clicked, this, [clearRecent = std::move(clearRecent)] {
        if (clearRecent) clearRecent();
    });

    auto* resources = new QGroupBox(tr("Learn and Explore"), this);
    auto* resourcesLayout = new QHBoxLayout(resources);
    auto* examples = resourceButton(tr("Open Example…"),
        tr("Browse example NEC models when available."), resources);
    auto* gettingStarted = resourceButton(tr("Getting Started"),
        tr("A short guide to the NEC Workbench workflow."), resources);
    auto* documentation = resourceButton(tr("Documentation"),
        tr("Card editing, model checks, analysis, and results."), resources);
    resourcesLayout->addWidget(examples);
    resourcesLayout->addWidget(gettingStarted);
    resourcesLayout->addWidget(documentation);
    connect(examples, &QCommandLinkButton::clicked, this,
        [openExamples = std::move(openExamples)] { if (openExamples) openExamples(); });
    connect(gettingStarted, &QCommandLinkButton::clicked, this, [this] {
        QMessageBox::information(this, tr("Getting Started"),
            tr("1. Create or open a NEC model.\n"
               "2. Edit geometry or NEC source cards.\n"
               "3. Run Check Model and resolve errors.\n"
               "4. Configure the solver and requested outputs.\n"
               "5. Run Analysis, then inspect numerical and radiation results."));
    });
    connect(documentation, &QCommandLinkButton::clicked, this, [this] {
        QMessageBox::information(this, tr("NEC Workbench Documentation"),
            tr("Use Geometry for graphical wire editing, NEC Source for raw and structured cards, "
               "Analysis for model setup and solver requests, and Results for plots and field views.\n\n"
               "Context menus provide wire, source, load, and transmission-line operations."));
    });

    pageLayout->addWidget(title);
    pageLayout->addWidget(subtitle);
    pageLayout->addLayout(primaryActions);
    pageLayout->addWidget(recentGroup, 1);
    pageLayout->addWidget(resources);
}

void WelcomePage::setRecentFiles(const QStringList& paths)
{
    recentFiles_->clear();
    for (const auto& path : paths) {
        auto* item = new QListWidgetItem(
            tr("%1\n%2").arg(QFileInfo(path).fileName(), path), recentFiles_);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setStatusTip(path);
    }
    const auto empty = paths.empty();
    noRecentFiles_->setVisible(empty);
    recentFiles_->setVisible(!empty);
}

}
