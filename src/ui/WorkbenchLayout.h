#pragma once

#include <QWidget>

class QAction;
class QSplitter;
class QStackedWidget;

namespace necwb::ui {

class BottomPanel;

// The main window's content. With no model, the Welcome page sits centered at the top; with a
// model, one splitter holds the model tree on the left, the geometry editor or a full
// result view in the center, and the results column on the right. The bottom panel spans
// the full width underneath.
class WorkbenchLayout final : public QWidget {
public:
    struct Panes {
        QWidget* welcome{};
        QWidget* leftColumn{};
        QWidget* geometry{};
        QWidget* fullView{};
        QWidget* rightColumn{};
        BottomPanel* bottomPanel{};
    };

    explicit WorkbenchLayout(Panes panes, QWidget* parent = nullptr);

    void showWelcome();
    void showModel();
    void showGeometry();
    void showFullView();
    [[nodiscard]] auto fullViewShown() const -> bool;
    // View › Results Column: shows or collapses the right column.
    [[nodiscard]] auto rightColumnAction() const -> QAction* { return rightColumnAction_; }
    void resetLayout();
    [[nodiscard]] auto minimumSizeHint() const -> QSize override;

protected:
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    void applyColumnWidths(int left, int right);
    void saveColumnWidths();
    void setRightColumnShown(bool shown);

    QStackedWidget* pages_{};
    QSplitter* split_{};
    QStackedWidget* center_{};
    QWidget* geometry_{};
    QWidget* fullView_{};
    BottomPanel* bottomPanel_{};
    QAction* rightColumnAction_{};
    // The saved widths are applied once the split first has a real width.
    bool columnWidthsApplied_{};
};

}
