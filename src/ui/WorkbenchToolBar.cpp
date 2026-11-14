#include "ui/WorkbenchToolBar.h"

#include "ui/analysis/AnalysisSetupEditor.h"

#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>

#include <array>

namespace necwb::ui {

WorkbenchToolBar::WorkbenchToolBar(const Actions& actions, QWidget* parent)
    : QToolBar(tr("Commands"), parent)
{
    setObjectName(QStringLiteral("workbenchToolBar"));
    setMovable(false);
    setFloatable(false);
    setIconSize({16, 16});
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // Short labels here; menus keep the full action names.
    const std::array labels{std::pair{actions.open, tr("Open")}, std::pair{actions.save, tr("Save")},
        std::pair{actions.run, tr("Run")}, std::pair{actions.stop, tr("Stop")},
        std::pair{actions.sweep, tr("Sweep")}, std::pair{actions.check, tr("Check")},
        std::pair{actions.optimize, tr("Optimize")}};
    for (const auto& [action, label] : labels) action->setIconText(label);

    addAction(actions.open);
    if (auto* openButton = qobject_cast<QToolButton*>(widgetForAction(actions.open))) {
        openButton->setMenu(actions.recentModels);
        openButton->setPopupMode(QToolButton::MenuButtonPopup);
        openButton->setObjectName(QStringLiteral("toolBarOpen"));
    }
    addAction(actions.save);
    addSeparator();
    backend_ = new QComboBox(this);
    backend_->setObjectName(QStringLiteral("toolBarBackend"));
    backend_->setToolTip(tr("Calculation engine"));
    for (const auto& [name, id] : AnalysisSetupEditor::backends()) backend_->addItem(name, id);
    backend_->insertSeparator(backend_->count());
    backend_->addItem(tr("Configure…"));
    addWidget(backend_);
    addAction(actions.run);
    addAction(actions.stop);
    addAction(actions.sweep);
    addAction(actions.check);
    addSeparator();
    addAction(actions.optimize);

    connect(backend_, &QComboBox::activated, this, [this](int index) {
        const auto id = backend_->itemData(index).toString();
        if (id.isEmpty()) {
            // "Configure…" is a command, not an engine: show the engine in use again.
            setBackend(backendId_);
            if (configure_) configure_();
            return;
        }
        backendId_ = id;
        if (backendSelected_) backendSelected_(id);
    });
}

void WorkbenchToolBar::setBackend(const QString& backendId)
{
    backendId_ = backendId;
    const QSignalBlocker blocker(backend_);
    const auto index = backend_->findData(backendId);
    if (index >= 0) backend_->setCurrentIndex(index);
}

void WorkbenchToolBar::setBackendSelectedCallback(std::function<void(const QString&)> callback)
{
    backendSelected_ = std::move(callback);
}

void WorkbenchToolBar::setConfigureCallback(std::function<void()> callback)
{
    configure_ = std::move(callback);
}

}
