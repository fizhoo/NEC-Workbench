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
        QAction* run{};
        QAction* stop{};
        QAction* sweep{};
        QAction* check{};
        QAction* optimize{};
    };

    explicit WorkbenchToolBar(const Actions& actions, QWidget* parent = nullptr);

    // Shows the engine without reporting a change.
    void setBackend(const QString& backendId);
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
