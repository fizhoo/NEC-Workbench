#include "ui/welcome/WelcomePage.h"

#include <QAction>
#include <QCommandLinkButton>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {

auto createSection(QAction* action, const QString& body, QWidget* parent) -> QCommandLinkButton*
{
    auto* button = new QCommandLinkButton(action->text().remove(QLatin1Char('&')), body, parent);
    button->setMinimumHeight(105);
    QObject::connect(button, &QCommandLinkButton::clicked, action, &QAction::trigger);
    return button;
}

}

WelcomePage::WelcomePage(QAction* newModelAction, QAction* openAction, QAction* geometryAction,
    QAction* sourceAction, QAction* analysisAction, QAction* resultsAction,
    QAction* optimizeAction, QWidget* parent)
    : QWidget(parent)
{
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(36, 30, 36, 30);
    pageLayout->setSpacing(22);

    auto* title = new QLabel(tr("NEC Workbench"), this);
    auto titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 10);
    title->setFont(titleFont);

    auto* subtitle = new QLabel(
        tr("Model, validate, analyze, optimize, and visualize NEC antenna designs."), this);
    auto subtitleFont = subtitle->font();
    subtitleFont.setPointSize(subtitleFont.pointSize() + 2);
    subtitle->setFont(subtitleFont);

    auto* actionsLayout = new QHBoxLayout;
    auto* newButton = new QPushButton(newModelAction->text().remove(QLatin1Char('&')), this);
    auto* openButton = new QPushButton(openAction->text().remove(QLatin1Char('&')), this);
    newButton->setDefault(true);
    connect(newButton, &QPushButton::clicked, newModelAction, &QAction::trigger);
    connect(openButton, &QPushButton::clicked, openAction, &QAction::trigger);
    actionsLayout->addWidget(newButton);
    actionsLayout->addWidget(openButton);
    actionsLayout->addStretch();

    auto* sections = new QGridLayout;
    sections->setSpacing(14);
    sections->addWidget(createSection(geometryAction,
        tr("Structured wire editing plus synchronized XY, XZ, YZ, and 3D geometry views."), this), 0, 0);
    sections->addWidget(createSection(sourceAction,
        tr("Edit the complete NEC deck in a dedicated source workspace."), this), 0, 1);
    sections->addWidget(createSection(analysisAction,
        tr("Configure the model, requests, and external NEC solver backends."), this), 1, 0);
    sections->addWidget(createSection(resultsAction,
        tr("Explore impedance, SWR, currents, gain, radiation patterns, and raw output."), this), 1, 1);
    sections->addWidget(createSection(optimizeAction,
        tr("Define variables, sweeps, objectives, constraints, and optimization runs."), this), 2, 0, 1, 2);

    pageLayout->addWidget(title);
    pageLayout->addWidget(subtitle);
    pageLayout->addLayout(actionsLayout);
    pageLayout->addLayout(sections, 1);
}

}
