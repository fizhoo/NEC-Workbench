#include "analysis/AnalysisResult.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/dashboard/DashboardPage.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/cards/WireCardEditor.h"
#include "ui/geometry/WirePropertiesDialog.h"
#include "nec/NecParser.h"
#include "model/WireGauge.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTableWidget>
#include <QPushButton>

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
    radiation2D.setSettingsChangedCallback([&radiation3D](const auto& settings) {
        radiation3D.setDisplaySettings(settings);
    });
    radiation3D.setSettingsChangedCallback([&radiation2D](const auto& settings) {
        radiation2D.setDisplaySettings(settings);
    });
    auto* radiation2DComponent = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DComponent"));
    auto* radiation3DComponent = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DComponent"));
    auto* radiation2DFloor = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DFloor"));
    auto* radiation3DFloor = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DFloor"));
    auto* radiation2DSummary = radiation2D.findChild<QLabel*>(QStringLiteral("radiation2DSummary"));
    auto* radiation3DSummary = radiation3D.findChild<QLabel*>(QStringLiteral("radiation3DSummary"));
    if (radiation2DComponent == nullptr || radiation3DComponent == nullptr
        || radiation2DFloor == nullptr || radiation3DFloor == nullptr
        || radiation2DSummary == nullptr || radiation3DSummary == nullptr) return EXIT_FAILURE;
    radiation2DComponent->setCurrentIndex(1);
    radiation3DFloor->setCurrentIndex(4);
    application.processEvents();
    const auto radiationControlsSynchronized = radiation3DComponent->currentData()
            == radiation2DComponent->currentData()
        && radiation2DFloor->currentData() == radiation3DFloor->currentData();
    const auto radiationMetricsVisible = radiation2DSummary->text().contains(QStringLiteral("beamwidth"))
        && radiation3DSummary->text().contains(QStringLiteral("peak"));
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
    necwb::ui::StructuredCardEditor structuredCards;
    structuredCards.resize(900, 500);
    structuredCards.setDocument(necwb::nec::NecParser{}.parse(
        "GW 1 11 0 0 -0.5 0 0 0.5 0.001\nEX 0 1 6 0 1 0\nFR 0 1 0 0 14.1 0\nEN\n"));
    QString editedCard;
    QString addedCard;
    std::size_t deletedLine{};
    QObject::connect(&structuredCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&editedCard](std::size_t, const QString& text) { editedCard = text; });
    QObject::connect(&structuredCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&structuredCards](std::size_t, const QString& text) {
            structuredCards.setDocument(necwb::nec::NecParser{}.parse(
                "GW 1 11 0 0 -0.5 0 0 0.5 0.001\nGE 0\n"
                + text.toStdString() + "\nFR 0 1 0 0 14.1 0\nEN\n"));
        });
    QObject::connect(&structuredCards, &necwb::ui::StructuredCardEditor::cardAddRequested,
        [&addedCard](const QString& text) { addedCard = text; });
    QObject::connect(&structuredCards, &necwb::ui::StructuredCardEditor::cardDeleteRequested,
        [&deletedLine](std::size_t sourceLine) { deletedLine = sourceLine; });
    auto* structuredTable = structuredCards.findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
    auto* structuredFamilies = structuredCards.findChild<QListWidget*>(
        QStringLiteral("structuredCardFamilies"));
    auto* structuredAdd = structuredCards.findChild<QPushButton*>(
        QStringLiteral("structuredAddCardButton"));
    if (structuredTable == nullptr || structuredFamilies == nullptr || structuredAdd == nullptr
        || structuredTable->rowCount() != 1 || !structuredAdd->isEnabled()) return EXIT_FAILURE;
    structuredFamilies->setCurrentRow(1);
    application.processEvents();
    const auto duplicateFrequencyBlocked = !structuredAdd->isEnabled();
    structuredFamilies->setCurrentRow(0);
    application.processEvents();
    structuredTable->item(0, 4)->setText(QStringLiteral("not-an-integer"));
    application.processEvents();
    const auto invalidEditBlocked = editedCard.isEmpty()
        && !structuredTable->item(0, 4)->toolTip().isEmpty();
    structuredTable->item(0, 4)->setText(QStringLiteral("7"));
    structuredCards.show(); application.processEvents();
    structuredTable->item(0, 8)->setText(QStringLiteral("2.5"));
    application.processEvents();
    structuredFamilies->setCurrentRow(3);
    application.processEvents();
    structuredAdd->click();
    application.processEvents();
    structuredFamilies->setCurrentRow(0);
    application.processEvents();
    structuredTable->selectRow(0);
    structuredCards.findChild<QPushButton*>(QStringLiteral("structuredDeleteCardButton"))->click();
    application.processEvents();
    structuredTable->openPersistentEditor(structuredTable->item(0, 2));
    application.processEvents();
    const auto cardDropdowns = structuredTable->findChildren<QComboBox*>();
    const auto descriptiveDropdown = cardDropdowns.size() == 1
        && cardDropdowns.front()->currentText().contains(QStringLiteral("Applied voltage source"))
        && cardDropdowns.front()->count() == 6;
    structuredTable->closePersistentEditor(structuredTable->item(0, 2));
    QImage structuredImage(900, 500, QImage::Format_ARGB32_Premultiplied);
    structuredImage.fill(Qt::transparent); QPainter structuredPainter(&structuredImage);
    structuredCards.render(&structuredPainter); structuredPainter.end();
    necwb::ui::StructuredCardEditor extendedCards;
    extendedCards.setDocument(necwb::nec::NecParser{}.parse(
        "FR 0 101 0 0 3.0 0.05 8.0 0 0 0\n"));
    auto* extendedFamilies = extendedCards.findChild<QListWidget*>(
        QStringLiteral("structuredCardFamilies"));
    auto* extendedTable = extendedCards.findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
    QString extendedFrequency;
    QObject::connect(&extendedCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&extendedFrequency](std::size_t, const QString& text) { extendedFrequency = text; });
    extendedFamilies->setCurrentRow(1);
    application.processEvents();
    extendedTable->item(0, 6)->setText(QStringLiteral("3.1"));
    application.processEvents();
    const auto trailingFieldsPreserved = extendedFrequency
        == QStringLiteral("FR 0 101 0 0 3.1 0.05 8.0 0 0 0");
    necwb::ui::WireCardEditor wireEditor;
    wireEditor.resize(900, 400);
    necwb::model::AntennaModel wireModel;
    wireModel.addWire({1, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 11, 0.001, 1});
    wireEditor.setModel(wireModel);
    auto wireEditCommitted = false;
    QObject::connect(&wireEditor, &necwb::ui::WireCardEditor::wireEdited,
        [&wireEditor, &wireEditCommitted](const necwb::model::Wire&, const necwb::model::Wire& updated) {
            necwb::model::AntennaModel refreshed;
            refreshed.addWire(updated);
            wireEditor.setModel(refreshed);
            wireEditCommitted = true;
        });
    auto* wireTable = wireEditor.findChild<QTableWidget*>();
    if (wireTable == nullptr || wireTable->rowCount() != 1) return EXIT_FAILURE;
    wireTable->item(0, 8)->setText(QStringLiteral("0.002"));
    wireEditor.show(); application.processEvents();
    QImage wireImage(900, 400, QImage::Format_ARGB32_Premultiplied);
    wireImage.fill(Qt::transparent); QPainter wirePainter(&wireImage);
    wireEditor.render(&wirePainter); wirePainter.end();
    necwb::model::AntennaModel gaugeModel;
    const auto gaugeRadius = necwb::model::awgRadiusMeters(12);
    const necwb::model::Wire gaugeWire{2, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
        11, gaugeRadius, 1};
    gaugeModel.addWire(gaugeWire);
    necwb::ui::WirePropertiesDialog wireProperties(gaugeWire, gaugeModel,
        necwb::model::LengthUnit::Meter);
    auto* gaugeEditor = wireProperties.findChild<QComboBox*>(QStringLiteral("wireGaugeDialogEditor"));
    const auto gaugeDropdownValid = gaugeEditor != nullptr
        && gaugeEditor->currentData().toInt() == 12;
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
    const auto passed = !image.isNull() && !fieldImage.isNull() && !currentImage.isNull()
        && !dashboardImage.isNull() && storeValid
        && !structuredImage.isNull() && invalidEditBlocked && duplicateFrequencyBlocked
        && descriptiveDropdown
        && trailingFieldsPreserved
        && editedCard == QStringLiteral("EX 0 1 7 0 1 0 2.5")
        && addedCard == QStringLiteral("LD 0 1 1 11 0 0 0") && deletedLine == 3
        && !wireImage.isNull() && wireEditCommitted && gaugeDropdownValid
        && radiationControlsSynchronized && radiationMetricsVisible;
    if (!passed) qWarning() << "structured smoke state" << invalidEditBlocked << editedCard
        << addedCard << deletedLine << "wire committed" << wireEditCommitted;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
