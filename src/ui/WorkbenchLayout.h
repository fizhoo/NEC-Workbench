#pragma once

#include <QWidget>

#include <optional>

class QAction;
class QSplitter;
class QStackedWidget;

namespace necwb::ui {

class BottomPanel;

// The main window's content. With no model, the Welcome page sits centered at the top; with a
// model, one splitter holds the model tree on the left, the geometry editor, a full result
// view or the NEC Deck in the center, and the results column on the right. The bottom panel spans
// the full width underneath.
class WorkbenchLayout final : public QWidget {
public:
    struct Panes {
        QWidget* welcome{};
        QWidget* leftColumn{};
        // Shown above the geometry.
        QWidget* setupLine{};
        QWidget* geometry{};
        QWidget* fullView{};
        QWidget* deck{};
        QWidget* rightColumn{};
        BottomPanel* bottomPanel{};
    };

    explicit WorkbenchLayout(Panes panes, QWidget* parent = nullptr);

    void showWelcome();
    void showModel();
    void showGeometry();
    void showFullView();
    void showDeck();
    [[nodiscard]] auto fullViewShown() const -> bool;
    // View › Results Column: shows or collapses the right column.
    [[nodiscard]] auto rightColumnAction() const -> QAction* { return rightColumnAction_; }
    void resetLayout();
    // Widens the left column to width for a wide page, at most a share of the window, and
    // returns true; a width the column had before puts it back and returns false.
    auto fitLeftColumn(int width) -> bool;
    void restoreLeftColumn();
    [[nodiscard]] auto minimumSizeHint() const -> QSize override;

protected:
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    void applyColumnWidths(int left, int right);
    void saveColumnWidths();
    void setRightColumnShown(bool shown);
    void expandLeftColumn(int width);

    QStackedWidget* pages_{};
    QSplitter* split_{};
    QStackedWidget* center_{};
    QWidget* geometry_{};
    QWidget* fullView_{};
    QWidget* deck_{};
    BottomPanel* bottomPanel_{};
    QAction* rightColumnAction_{};
    // The column widths, and the left one's maximum, before it was widened for a table.
    struct Widths {
        int left{};
        int right{};
        int maximum{};
    };
    std::optional<Widths> expandedFrom_;
    // The saved widths are applied once the split first has a real width.
    bool columnWidthsApplied_{};
};

}
