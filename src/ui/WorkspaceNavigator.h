#pragma once

#include <QString>
#include <QWidget>

class QListWidget;
class QTabWidget;
class QToolButton;

namespace necwb::ui {

class WorkspaceNavigator final : public QWidget {
public:
    explicit WorkspaceNavigator(QTabWidget* pages, QString settingsId,
        QWidget* parent = nullptr);

    [[nodiscard]] auto pages() const noexcept -> QTabWidget*;
    [[nodiscard]] auto isCollapsed() const noexcept -> bool;
    void setCollapsed(bool collapsed);

private:
    void populate();
    void updatePresentation();

    QTabWidget* pages_{};
    QListWidget* navigation_{};
    QToolButton* collapseButton_{};
    QString settingsId_;
    bool collapsed_{};
};

}
