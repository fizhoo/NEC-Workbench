#include "analysis/AnalysisResult.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/dashboard/DashboardPage.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextDocument>

#include <cstdlib>
#include <cmath>
#include <vector>

auto main(int argc, char* argv[]) -> int
{
    QApplication application(argc, argv);
    necwb::analysis::AnalysisResult result;
    result.feedpoints = {
        {14.0, 1, 6, {1.0, 0.0}, {0.01, 0.0}, {40.0, -12.0}, 0.005},
        {14.1, 1, 6, {1.0, 0.0}, {0.01, 0.0}, {50.0, 0.0}, 0.005},
        {14.2, 1, 6, {1.0, 0.0}, {0.01, 0.0}, {63.0, 18.0}, 0.005},
    };
    result.currents = {
        {14.1, 1, 1, 0, 0, -0.1, 0.05, {0.01, 0.0}, 0.01, 0.0},
        {14.1, 2, 1, 0, 0, 0.1, 0.05, {0.008, 0.002}, 0.00825, 14.0},
    };
    for (auto phi = 0; phi < 360; phi += 30) {
        for (auto theta = 0; theta <= 180; theta += 15) {
            const auto gain = 2.15 + 20.0 * std::log10(std::max(0.001,
                std::abs(std::sin(theta * 3.14159265358979323846 / 180.0))));
            result.radiation.push_back({14.1, static_cast<double>(theta),
                static_cast<double>(phi), gain, -999.99, gain});
        }
    }
    necwb::ui::SweepPlotsView view;
    view.resize(900, 700);
    view.setResults(result, QStringLiteral("test-run"));
    view.show();
    application.processEvents();

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    view.render(&painter);
    painter.end();
    necwb::ui::CurrentDistributionView currents;
    necwb::ui::RadiationPatternView radiation2D;
    necwb::ui::Radiation3DView radiation3D;
    for (auto* resultView : std::vector<QWidget*>{&currents, &radiation2D, &radiation3D}) {
        resultView->resize(800, 600);
    }
    currents.setResults(result, QStringLiteral("test-run"));
    radiation2D.setResults(result, QStringLiteral("test-run"));
    necwb::model::AntennaModel antenna;
    antenna.addWire({1, {0.0, 0.0, -0.5}, {0.0, 0.0, 0.5}, 11, 0.001, 1});
    radiation3D.setModel(antenna);
    radiation3D.setResults(result, QStringLiteral("test-run"));
    currents.show(); radiation2D.show(); radiation3D.show(); application.processEvents();
    QImage fieldImage(800, 600, QImage::Format_ARGB32_Premultiplied);
    fieldImage.fill(Qt::transparent); QPainter fieldPainter(&fieldImage);
    currents.render(&fieldPainter); radiation2D.render(&fieldPainter); radiation3D.render(&fieldPainter);
    fieldPainter.end();
    necwb::analysis::AnalysisResult currentOnly;
    currentOnly.currents = result.currents;
    radiation3D.setResults(currentOnly, QStringLiteral("Model: test.nec · Run: current-only"));
    application.processEvents();
    QImage currentImage(800, 600, QImage::Format_ARGB32_Premultiplied);
    currentImage.fill(Qt::transparent); QPainter currentPainter(&currentImage);
    radiation3D.render(&currentPainter); currentPainter.end();
    QTextDocument sourceDocument(QStringLiteral("CM dashboard test\nCE\nGW 1 11 0 0 -0.5 0 0 0.5 0.001\nGE 0\nEN\n"));
    necwb::ui::DashboardPage dashboard(&sourceDocument);
    dashboard.resize(1000, 700);
    necwb::model::ModelSetup setup;
    dashboard.setModel(antenna, setup, QStringLiteral("nec2"), true, 0, 0);
    dashboard.setResults(result, QStringLiteral("Model: test.nec · Run: dashboard"), false);
    dashboard.show(); application.processEvents();
    QImage dashboardImage(1000, 700, QImage::Format_ARGB32_Premultiplied);
    dashboardImage.fill(Qt::transparent); QPainter dashboardPainter(&dashboardImage);
    dashboard.render(&dashboardPainter); dashboardPainter.end();
    QTemporaryDir directory;
    necwb::ui::AnalysisRunStore store(directory.path());
    auto run = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"));
    run.status = QStringLiteral("Completed");
    run.durationSeconds = 1.25;
    if (!store.save(run)) {
        return EXIT_FAILURE;
    }
    const auto loaded = store.load();
    const auto storeValid = loaded.size() == 1
        && loaded.front().status == QStringLiteral("Completed")
        && loaded.front().sourceFile == QStringLiteral("/tmp/test-dipole.nec")
        && loaded.front().backend == QStringLiteral("nec2")
        && loaded.front().durationSeconds == 1.25;
    return !image.isNull() && !fieldImage.isNull() && !currentImage.isNull()
        && !dashboardImage.isNull() && storeValid
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
