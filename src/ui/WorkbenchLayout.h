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

private:
    void saveLeftColumnWidth() const;

    QStackedWidget* pages_{};
    QSplitter* split_{};
};

}
