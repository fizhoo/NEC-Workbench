#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>

class QAction;
class QLabel;
class QTreeWidget;

namespace necwb::ui {

// The center with no model open: New, Open and Open Example, and the recent models with
// their folder and when each was last opened. It sits at the top and does not stretch.
class StartPanel final : public QWidget {
public:
    StartPanel(QAction* newModel, QAction* open, std::function<void()> openExample,
        std::function<void(const QString&)> openRecent, QWidget* parent = nullptr);
    void setRecentFiles(const QStringList& paths);

protected:
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void fitRecentList();
    QTreeWidget* recent_{};
    QLabel* noRecent_{};
};

}
