#pragma once

#include <QToolBar>

#include <functional>

class QAction;
class QComboBox;
class QMenu;

namespace necwb::ui {

// The one row of commands across the top of the main window.
class WorkbenchToolBar final : public QToolBar {
public:
    struct Actions {
        QAction* open{};
        // Shown from the arrow beside Open.
        QMenu* recentModels{};
        QAction* save{};
        QAction* revert{};
        QAction* undo{};
        QAction* redo{};
        QAction* run{};
        QAction* stop{};
        QAction* sweep{};
        QAction* check{};
        QAction* optimize{};
    };

    explicit WorkbenchToolBar(const Actions& actions, QWidget* parent = nullptr);

    // Shows the engine without reporting a change, with its executable in the tooltip.
    // The engine in use, its name in the list and the tooltip saying which executable it is.
    void setBackend(const QString& backendId, const QString& label, const QString& description);
    void setBackendSelectedCallback(std::function<void(const QString&)> callback);
    // Called for "Configure…" in the engine list.
    void setConfigureCallback(std::function<void()> callback);

private:
    QComboBox* backend_{};
    QString backendId_;
    std::function<void(const QString&)> backendSelected_;
    std::function<void()> configure_;
};

}
