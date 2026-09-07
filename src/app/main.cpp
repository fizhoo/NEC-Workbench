#include "ui/MainWindow.h"

#include <QApplication>
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
    auto applicationFont = application.font();
    applicationFont.setPointSize(workspaceDensityFontSize());
    application.setFont(applicationFont);

    necwb::ui::MainWindow window;
    window.show();
    return application.exec();
}
