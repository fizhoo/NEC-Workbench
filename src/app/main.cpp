#include "ui/MainWindow.h"
#include "ui/PackageCheck.h"
#include "ui/WorkspaceDensity.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include <optional>
#include <string_view>

auto main(int argc, char* argv[]) -> int
{
    // Checks a built package (docs/architecture.md, Solver Integration); never opens a window.
    if (argc == 2 && std::string_view(argv[1]) == "--package-check") {
        QCoreApplication application(argc, argv);
        return necwb::ui::runPackageCheck();
    }
    // Starts as normal and quits once the main window is on screen (PackageCheck.h).
    const auto startupCheck = argc == 2 && std::string_view(argv[1]) == "--startup-check";
    QApplication application(argc, argv);
    QApplication::setApplicationName("NEC Workbench");
    QApplication::setOrganizationName("NEC Workbench");
    QApplication::setApplicationVersion(QStringLiteral(NECWB_DISPLAY_VERSION));
    std::optional<necwb::ui::ThrowawaySettings> throwawaySettings;
    if (startupCheck) throwawaySettings.emplace();
    auto applicationFont = application.font();
    applicationFont.setPointSize(necwb::ui::workspaceDensityFontSize(necwb::ui::savedWorkspaceDensity()));
    application.setFont(applicationFont);

    necwb::ui::MainWindow window;
    window.show();
    if (startupCheck) {
        necwb::ui::watchStartup(window);
        return application.exec();
    }
    // Holding Shift while the app starts skips reopening the last model.
    const auto skipReopen = QApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
    QTimer::singleShot(0, &window, [&window, skipReopen] { window.reopenLastModel(skipReopen); });
    return application.exec();
}
