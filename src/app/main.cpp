#include "ui/MainWindow.h"

#include <QApplication>
#include <QTimer>
#include <QSettings>

namespace {

auto workspaceDensityFontSize() -> int
{
    const auto density = QSettings{}.value(QStringLiteral("appearance/workspaceDensity"),
        QStringLiteral("compact")).toString();
    if (density == QStringLiteral("spacious")) return 11;
    if (density == QStringLiteral("standard")) return 10;
    return 9;
}

}

auto main(int argc, char* argv[]) -> int
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("NEC Workbench");
    QApplication::setOrganizationName("NEC Workbench");
    QApplication::setApplicationVersion(QStringLiteral(NECWB_DISPLAY_VERSION));
    auto applicationFont = application.font();
    applicationFont.setPointSize(workspaceDensityFontSize());
    application.setFont(applicationFont);

    necwb::ui::MainWindow window;
    window.show();
    // Holding Shift while the app starts skips reopening the last model.
    const auto skipReopen = QApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
    QTimer::singleShot(0, &window, [&window, skipReopen] { window.reopenLastModel(skipReopen); });
    return application.exec();
}
