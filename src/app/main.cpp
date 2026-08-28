#include "ui/MainWindow.h"

#include <QApplication>

auto main(int argc, char* argv[]) -> int
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("NEC Workbench");
    QApplication::setOrganizationName("NEC Workbench");

    necwb::ui::MainWindow window;
    window.show();
    return application.exec();
}
