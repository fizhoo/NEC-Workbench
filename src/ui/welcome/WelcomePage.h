#pragma once

#include <QWidget>
#include <QStringList>

#include <functional>

class QAction;
class QLabel;
class QListWidget;

namespace necwb::ui {

class WelcomePage final : public QWidget {
public:
    using PathCallback = std::function<void(const QString&)>;
    using VoidCallback = std::function<void()>;

    WelcomePage(QAction* newModelAction, QAction* openAction, PathCallback openRecent,
        VoidCallback openExamples, VoidCallback clearRecent, QWidget* parent = nullptr);
    void setRecentFiles(const QStringList& paths);

private:
    PathCallback openRecent_;
    QListWidget* recentFiles_{};
    QLabel* noRecentFiles_{};
};

}
