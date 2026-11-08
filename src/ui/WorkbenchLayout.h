#pragma once

#include <QWidget>

class QSplitter;
class QStackedWidget;

namespace necwb::ui {

class BottomPanel;

// The main window's content. With no model, the Welcome page sits centered at the top; with a
// model, the control panel is a left column beside the center view. The bottom panel spans
// the full width underneath either one.
class WorkbenchLayout final : public QWidget {
public:
    WorkbenchLayout(QWidget* welcome, QWidget* leftColumn, QWidget* center,
        BottomPanel* bottomPanel, QWidget* parent = nullptr);

    void showWelcome();
    void showModel();
    void resetLayout();
    [[nodiscard]] auto minimumSizeHint() const -> QSize override;

protected:
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    void applyLeftColumnWidth(int width);
    void saveLeftColumnWidth() const;

    QStackedWidget* pages_{};
    QSplitter* split_{};
    BottomPanel* bottomPanel_{};
    // The saved width is applied once the split first has a real width.
    bool leftColumnWidthApplied_{};
};

}
