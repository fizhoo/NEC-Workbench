#include "ui/optimization/OptimizerWindow.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {
namespace {

const auto GeometryKey = QStringLiteral("optimizerWindow/geometry");

}

OptimizerWindow::OptimizerWindow(QWidget* content, QWidget* mainWindow)
    : QWidget(nullptr, Qt::Window), mainWindow_(mainWindow)
{
    setObjectName(QStringLiteral("optimizerWindow"));
    // Closing it alone never quits the application.
    setAttribute(Qt::WA_QuitOnClose, false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
}

void OptimizerWindow::present()
{
    if (!isVisible()) {
        const auto saved = QSettings{}.value(GeometryKey).toByteArray();
        if (saved.isEmpty() || !restoreGeometry(saved)
            || QGuiApplication::screenAt(geometry().center()) == nullptr)
            placeFirst();
    }
    if (isMinimized()) showNormal();
    else show();
    raise();
    activateWindow();
}

void OptimizerWindow::closeEvent(QCloseEvent* event)
{
    QSettings{}.setValue(GeometryKey, saveGeometry());
    QWidget::closeEvent(event);
}

void OptimizerWindow::placeFirst()
{
    // About 80% of the main window, kept off the geometry where the screens allow: on
    // another monitor, else beside the main window, else over its right-hand side.
    const auto main = mainWindow_->frameGeometry();
    auto* mainScreen = mainWindow_->screen();
    const auto wanted = main.size() * 0.8;
    const auto fitIn = [this, wanted](const QRect& area) {
        resize(wanted.boundedTo(area.size() * 0.95));
        return area;
    };
    for (auto* screen : QGuiApplication::screens()) {
        if (screen == mainScreen) continue;
        const auto area = fitIn(screen->availableGeometry());
        move(area.center() - rect().center());
        return;
    }
    const auto area = fitIn(mainScreen->availableGeometry());
    auto x = 0;
    if (area.right() - main.right() >= width()) x = main.right() + 1;
    else if (main.left() - area.left() >= width()) x = main.left() - width();
    else x = main.right() - width();
    const auto y = std::clamp(main.top() + 40, area.top(), std::max(area.top(), area.bottom() - height()));
    move(std::clamp(x, area.left(), std::max(area.left(), area.right() - width())), y);
}

}
