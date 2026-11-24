#pragma once

#include <QWidget>

namespace necwb::ui {

// The optimizer's own top-level window. It has no parent, so it and the main window come to
// the front independently; the main window closes it. Closing only hides it, so the study
// and a running search live on, and it reopens where it was left.
class OptimizerWindow final : public QWidget {
public:
    OptimizerWindow(QWidget* content, QWidget* mainWindow);

    // Shows the window, placing it on first use, or brings it to the front.
    void present();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void placeFirst();

    QWidget* mainWindow_;
};

}
