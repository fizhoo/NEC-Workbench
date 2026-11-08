#pragma once

#include <QWidget>

class QStackedWidget;
class QTabBar;

namespace necwb::ui {

class ProblemsView;
class RunLogView;

// Tabbed panel below the dashboard. Collapsed it is just its tab strip; clicking a tab opens
// it and clicking the open tab collapses it again.
class BottomPanel final : public QWidget {
    Q_OBJECT

public:
    enum class Tab { Problems, RunLog };

    explicit BottomPanel(QWidget* parent = nullptr);

    [[nodiscard]] auto problems() const -> ProblemsView* { return problems_; }
    [[nodiscard]] auto runLog() const -> RunLogView* { return runLog_; }

    [[nodiscard]] auto isCollapsed() const -> bool { return collapsed_; }
    [[nodiscard]] auto currentTab() const -> Tab;
    void showTab(Tab tab);
    void setCollapsed(bool collapsed);
    void resetLayout();
    // Opens the Run log for a solver run; a successful run collapses it again when the
    // panel was collapsed before the run opened it.
    void openForRun();
    void finishRun(bool succeeded);
    // Sizes the panel for the window (the window is fixed to its content for now): open, it
    // takes its remembered height, cut down to fit availableHeight.
    auto fitHeight(int availableHeight) -> int;

signals:
    void heightChanged();

private:
    void saveState() const;

    QTabBar* tabs_{};
    QStackedWidget* pages_{};
    QWidget* resizeGrip_{};
    ProblemsView* problems_{};
    RunLogView* runLog_{};
    int openHeight_{220};
    bool collapsed_{true};
    bool collapsedBeforeRun_{};
};

}
