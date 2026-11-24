#include "analysis/AnalysisResult.h"
#include "analysis/FrequencyComparison.h"
#include "ui/DisplayFormat.h"
#include "ui/MainWindow.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/WorkbenchLayout.h"
#include "ui/WorkbenchToolBar.h"
#include "ui/results/ResultFullView.h"
#include "ui/results/ResultsColumn.h"
#include "ui/results/ResultsController.h"
#include "ui/analysis/AnalysisSetupEditor.h"
#include "ui/analysis/AnalysisRunStore.h"
#include "ui/analysis/ResultsSummaryView.h"
#include "ui/analysis/RunReviewWindow.h"
#include "ui/analysis/AverageGainResultsView.h"
#include "ui/analysis/AnalysisRequestEditor.h"
#include "ui/analysis/ConvergenceWorkspace.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/analysis/HistoricalOverlayControl.h"
#include "ui/cards/StructuredCardEditor.h"
#include "ui/cards/CompactNumberDelegate.h"
#include "ui/cards/SymbolCells.h"
#include "ui/cards/WireCardEditor.h"
#include "ui/editor/NecEditor.h"
#include "ui/PendingEditIndicator.h"
#include "ui/modeltree/ModelColumn.h"
#include "ui/modeltree/ModelTree.h"
#include "ui/modeltree/PropertiesPane.h"
#include "ui/modeltree/VariablesSection.h"
#include "ui/analysis/RadiationSurfaceMesh.h"
#include "ui/FlowLayout.h"
#include "ui/geometry/AutoSegmentationDialog.h"
#include "ui/ColorScale.h"
#include "ui/geometry/GeometryPlanes.h"
#include "ui/geometry/GeometryWorkspace.h"
#include "model/WireGeometry.h"
#include "ui/geometry/GeometryView.h"
#include "ui/geometry/Geometry3DView.h"
#include "ui/optimization/OptimizationWorkspace.h"
#include "ui/optimization/OptimizationComparePanel.h"
#include "ui/model/ParameterEditor.h"
#include "ui/setup/SetupEditor.h"
#include "ui/start/RecentModels.h"
#include "ui/start/StartPanel.h"
#include "ui/SettingsCleanup.h"
#include "ui/SolverLocator.h"
#include "ui/SolverSettings.h"
#include "nec/NecParser.h"
#include "model/WireGauge.h"

#include <QApplication>
#include <array>
#include <functional>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QDialog>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QAbstractItemDelegate>
#include <QMouseEvent>
#include <QSpinBox>
#include <QPainter>
#include <QPalette>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextCursor>
#include <QTextDocument>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QPushButton>
#include <QWheelEvent>
#include <QShortcut>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QTest>
#include <QThread>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

namespace {

auto smokeFailure(const char* checkpoint, int line) -> int
{
    std::fprintf(stderr, "Plot smoke failure at line %d: %s\n", line, checkpoint);
    std::fflush(stderr);
    return EXIT_FAILURE;
}

// An executable's file name as the PATH and the bundled lookup find it.
#ifdef Q_OS_WIN
const auto ExecutableSuffix = QStringLiteral(".exe");
#else
const auto ExecutableSuffix = QString{};
#endif

auto topLevelDialog(const QString& objectName) -> QDialog*
{
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == objectName) return qobject_cast<QDialog*>(widget);
    }
    return nullptr;
}

// The optimizer lives in its own top-level window, outside the main window's children.
template <typename T>
auto optimizerChild(const QString& objectName) -> T*
{
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("optimizerWindow"))
            return widget->findChild<T*>(objectName);
    }
    return nullptr;
}

}

#define NECWB_SMOKE_FAILURE(checkpoint) smokeFailure(checkpoint, __LINE__)

// With NECWB_SMOKE_TRACE set, each top-level block says when it starts, so a run that hangs
// shows where.
#define NECWB_SMOKE_STEP() \
    do { \
        if (qEnvironmentVariableIsSet("NECWB_SMOKE_TRACE")) { \
            std::fprintf(stderr, "step at line %d\n", __LINE__); \
            std::fflush(stderr); \
        } \
    } while (false)

auto main(int argc, char* argv[]) -> int
{
    if (argc == 3 && QByteArray(argv[1]).startsWith("-i")
        && QByteArray(argv[2]).startsWith("-o")) {
        QFile output(QString::fromLocal8Bit(argv[2] + 2));
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return EXIT_FAILURE;
        // With an inputs folder set, keep each deck there and answer at its FR frequency.
        if (const auto inputs = qEnvironmentVariable("NECWB_FAKE_SOLVER_INPUTS");
            !inputs.isEmpty()) {
            const auto inputPath = QString::fromLocal8Bit(argv[1] + 2);
            QFile::copy(inputPath, QStringLiteral("%1/input-%2.nec").arg(inputs)
                .arg(QCoreApplication::applicationPid()));
            QFile input(inputPath);
            auto frequency = QByteArray("0");
            if (input.open(QIODevice::ReadOnly | QIODevice::Text)) {
                for (const auto& line : input.readAll().split('\n')) {
                    const auto fields = line.simplified().split(' ');
                    if (fields.value(0) == "FR" && fields.size() > 5) frequency = fields[5];
                }
            }
            const auto answer = QByteArray(" FREQUENCY : ") + QByteArray::number(
                frequency.toDouble(), 'E', 4) + " MHz\n"
                " --------- ANTENNA INPUT PARAMETERS ---------\n"
                " 1 6 1.0E+00 0.0E+00 1.2536E-02 -2.9698E-03 7.5534E+01 1.7895E+01 0 0 6.2678E-03\n";
            return output.write(answer) == answer.size() ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        const QByteArray solverOutput =
            " FREQUENCY : 7.0000E+00 MHz\n"
            " --------- ANTENNA INPUT PARAMETERS ---------\n"
            " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n"
            " FREQUENCY : 1.4000E+01 MHz\n"
            " --------- ANTENNA INPUT PARAMETERS ---------\n"
            " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 7.5E+01 1.5E+01 0 0 5.0E-03\n";
        return output.write(solverOutput) == solverOutput.size() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NEC Workbench Tests"));
    QApplication::setOrganizationName(QStringLiteral("NEC Workbench"));
    const auto hasCompactMargins = [](const QWidget& widget, int maximum) {
        const auto* layout = widget.layout();
        if (layout == nullptr) return false;
        const auto margins = layout->contentsMargins();
        return margins.left() <= maximum && margins.top() <= maximum
            && margins.right() <= maximum && margins.bottom() <= maximum;
    };
    necwb::ui::AnalysisSetupEditor solverSetup;
    const auto solverSetupCompact = hasCompactMargins(solverSetup, 12);
    auto* solverBackend = solverSetup.findChild<QComboBox*>(
        QStringLiteral("analysisBackendControl"));
    auto* solverExecutable = solverSetup.findChild<QLineEdit*>(
        QStringLiteral("analysisExecutableControl"));
    const auto backendChoicesValid = solverBackend != nullptr
        && solverExecutable != nullptr && solverBackend->count() == 3
        && solverBackend->itemData(0).toString() == QStringLiteral("nec2")
        && solverBackend->itemData(1).toString() == QStringLiteral("opennec")
        && solverBackend->itemData(2).toString() == QStringLiteral("nec2dxs");
    const QHash<QString, QString> solverPaths{
        {QStringLiteral("nec2"), QCoreApplication::applicationFilePath()},
        {QStringLiteral("opennec"), QStringLiteral("/tmp/onec")},
        {QStringLiteral("nec2dxs"), QStringLiteral("/tmp/Nec2dXS11k.exe")},
    };
    solverSetup.setSettings(QStringLiteral("nec2"), solverPaths, 120);
    if (solverBackend != nullptr) solverBackend->setCurrentIndex(2);
    const auto backendChangeRestoresExecutable = solverExecutable != nullptr
        && solverExecutable->text() == QStringLiteral("/tmp/Nec2dXS11k.exe");
    if (solverBackend != nullptr) solverBackend->setCurrentIndex(0);
    const auto backendReturnRestoresExecutable = solverExecutable != nullptr
        && solverExecutable->text() == QCoreApplication::applicationFilePath();
    // Switching engines reports no path: only one typed or browsed to is the user's to save.
    QStringList pathsChosen;
    QObject::connect(&solverSetup, &necwb::ui::AnalysisSetupEditor::executablePathChanged,
        [&pathsChosen](const QString& backend, const QString& path) { pathsChosen << backend + QLatin1Char('=') + path; });
    solverSetup.selectBackend(QStringLiteral("opennec"));
    solverSetup.selectBackend(QStringLiteral("nec2"));
    const auto switchingSavesNothing = pathsChosen.isEmpty();
    if (solverExecutable != nullptr) {
        solverExecutable->setText(QStringLiteral("/opt/my/nec2c"));
        emit solverExecutable->editingFinished();
    }
    const auto typedPathChosen = pathsChosen == QStringList{QStringLiteral("nec2=/opt/my/nec2c")};
    if (!backendChoicesValid || !backendChangeRestoresExecutable
        || !backendReturnRestoresExecutable || !switchingSavesNothing || !typedPathChosen)
        return NECWB_SMOKE_FAILURE("solver backend selection");
    // Finding an engine: the path the user chose, even one that has gone; else the copy
    // beside Workbench or in solvers/; else the PATH. Only a chosen path outside Workbench's
    // folder is saved, and a bundled build's version comes from its metadata.
    NECWB_SMOKE_STEP();
    {
        using necwb::ui::LocatedSolver;
        QTemporaryDir application, elsewhere;
        const auto makeExecutable = [](const QString& path, const QByteArray& script) {
            QDir().mkpath(QFileInfo(path).path());
            QFile file(path);
            file.open(QIODevice::WriteOnly);
            file.write(script);
            file.close();
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        };
        const auto savedPath = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(elsewhere.path()));
        const auto app = application.path();
        const auto located = [&app](const QString& chosen) {
            return necwb::ui::locateSolver(QStringLiteral("nec2"), chosen, app);
        };
        const auto nothing = located({}).origin == LocatedSolver::Origin::NotFound;
        makeExecutable(elsewhere.filePath(QStringLiteral("nec2c") + ExecutableSuffix),
            "#!/bin/sh\n[ \"$1\" = -v ] && echo 'nec2c 9.9 (on path)'\n");
        const auto onPath = located({}).origin == LocatedSolver::Origin::Path;
        makeExecutable(app + QStringLiteral("/solvers/nec2c") + ExecutableSuffix, "#!/bin/sh\n");
        const auto inSolvers = located({}).origin == LocatedSolver::Origin::Bundled
            && QDir::fromNativeSeparators(located({}).path).endsWith(QStringLiteral("solvers/nec2c") + ExecutableSuffix);
        makeExecutable(app + QStringLiteral("/nec2c") + ExecutableSuffix, "#!/bin/sh\n");
        QFile metadata(app + QStringLiteral("/nec2c-build.txt"));
        metadata.open(QIODevice::WriteOnly);
        metadata.write("name=nec2c\nversion=1.3.3\ncommit=55be1e0e3fe5ee9dad4ce6050711450d19c562fd\n");
        metadata.close();
        const auto bundled = located({});
        const auto besideFirst = bundled.origin == LocatedSolver::Origin::Bundled
            && bundled.path == QDir::toNativeSeparators(app + QStringLiteral("/nec2c") + ExecutableSuffix)
            && necwb::ui::solverLabel(QStringLiteral("nec2"), bundled) == QStringLiteral("nec2c (bundled)")
            && necwb::ui::solverDescription(QStringLiteral("nec2"), bundled)
                .startsWith(QStringLiteral("nec2c 1.3.3 (55be1e0) · bundled · "));
        const auto chosen = located(elsewhere.filePath(QStringLiteral("nec2c") + ExecutableSuffix));
        const auto gone = located(QStringLiteral("/gone/nec2c"));
        const auto chosenFirst = chosen.origin == LocatedSolver::Origin::Chosen && !chosen.missing()
            && necwb::ui::solverLabel(QStringLiteral("nec2"), chosen) == QStringLiteral("NEC-2 / nec2c")
            && gone.origin == LocatedSolver::Origin::Chosen && gone.missing()
            && necwb::ui::solverDescription(QStringLiteral("nec2"), gone).contains(QStringLiteral("no longer exists"));
        // A user's nec2c is asked its version in the background; another program never is.
        // (On Windows the stand-in cannot run, so its version is unknown, without waiting.)
        auto& versions = necwb::ui::SolverVersions::instance();
        const auto userNec2c = elsewhere.filePath(QStringLiteral("nec2c") + ExecutableSuffix);
        const auto asking = versions.version(QStringLiteral("nec2"), userNec2c).isEmpty();
        QElapsedTimer probing;
        probing.start();
        while (versions.version(QStringLiteral("nec2"), userNec2c).isEmpty() && probing.elapsed() < 3000)
            QTest::qWait(20);
        const auto probed = asking && versions.version(QStringLiteral("nec2"), userNec2c)
            == (ExecutableSuffix.isEmpty() ? QStringLiteral("nec2c 9.9 (on path)") : QStringLiteral("version unknown"));
        makeExecutable(elsewhere.filePath(QStringLiteral("other") + ExecutableSuffix), "#!/bin/sh\nexit 1\n");
        const auto notAsked = versions.version(QStringLiteral("nec2"),
            elsewhere.filePath(QStringLiteral("other") + ExecutableSuffix))
            == QStringLiteral("version unknown");
        // Saving: a path inside Workbench's own folder or the mounted AppImage is refused.
        QSettings settings;
        const auto savedChoice = settings.value(QStringLiteral("analysis/executables/nec2"));
        const auto refusesOwnFolder = !necwb::ui::solverSettings::choosePath(QStringLiteral("nec2"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/nec2c"));
        qputenv("APPDIR", QFile::encodeName(app));
        const auto refusesAppImage = !necwb::ui::solverSettings::choosePath(QStringLiteral("nec2"),
            app + QStringLiteral("/usr/bin/nec2c"));
        qunsetenv("APPDIR");
        const auto savesChosen = necwb::ui::solverSettings::choosePath(QStringLiteral("nec2"), userNec2c)
            && necwb::ui::solverSettings::chosenPaths().value(QStringLiteral("nec2")) == userNec2c
            && necwb::ui::solverSettings::choosePath(QStringLiteral("nec2"), {})
            && !settings.contains(QStringLiteral("analysis/executables/nec2"));
        if (savedChoice.isValid()) settings.setValue(QStringLiteral("analysis/executables/nec2"), savedChoice);
        // A run records the engine build that made it.
        QTemporaryDir runs;
        necwb::ui::AnalysisRunStore store(runs.path());
        const auto record = store.create(QStringLiteral("nec2"), {}, QStringLiteral("analysis"), {}, bundled.path);
        const auto reloaded = store.load();
        const auto runRecordsEngine = record.engineVersion == QStringLiteral("nec2c 1.3.3 (55be1e0)")
            && reloaded.size() == 1 && reloaded.front().engineVersion == record.engineVersion
            && reloaded.front().enginePath == bundled.path;
        qputenv("PATH", savedPath);
        if (!nothing || !onPath || !inSolvers || !besideFirst || !chosenFirst || !probed || !notAsked
            || !refusesOwnFolder || !refusesAppImage || !savesChosen || !runRecordsEngine) {
            qWarning() << "solver lookup" << nothing << onPath << inSolvers << besideFirst << chosenFirst
                       << probed << notAsked << refusesOwnFolder << refusesAppImage << savesChosen
                       << runRecordsEngine << versions.version(QStringLiteral("nec2"), userNec2c);
            return NECWB_SMOKE_FAILURE("solver lookup and saved paths");
        }
    }
    necwb::ui::NecEditor historyEditor;
    const auto originalSource = QStringLiteral("CM original\nCE\nGE 0\nEN");
    const auto typedSource = QStringLiteral("CM typed\nCM original\nCE\nGE 0\nEN");
    const auto structuredSource = QStringLiteral(
        "CM typed\nCM original\nCE\nGW 1 11 0 0 0 1 0 0 0.001\nGE 0\nEN");
    historyEditor.setPlainText(originalSource);
    historyEditor.moveCursor(QTextCursor::Start);
    historyEditor.insertPlainText(QStringLiteral("CM typed\n"));
    historyEditor.replaceTextAsSingleEdit(structuredSource);
    const auto combinedHistoryRecorded = historyEditor.toPlainText() == structuredSource
        && historyEditor.document()->isUndoAvailable();
    historyEditor.undo();
    const auto structuredUndoRestoresTyping = historyEditor.toPlainText() == typedSource;
    historyEditor.undo();
    const auto typingUndoRestoresOriginal = historyEditor.toPlainText() == originalSource;
    historyEditor.redo();
    historyEditor.redo();
    const auto combinedHistoryRedoes = historyEditor.toPlainText() == structuredSource;
    if (!combinedHistoryRecorded || !structuredUndoRestoresTyping
        || !typingUndoRestoresOriginal || !combinedHistoryRedoes) {
        return NECWB_SMOKE_FAILURE("unified source undo history");
    }
    if (necwb::ui::formatDecimal(1.2) != QStringLiteral("1.200")
        || necwb::ui::formatDecimal(0.0004) != QStringLiteral("4.000e-04")
        || necwb::ui::formatAxisTick(13.8, 0.1) != QStringLiteral("13.8")
        || necwb::ui::formatAxisTick(14.3, 0.1) != QStringLiteral("14.3")
        || necwb::ui::formatAxisTick(4.6, 0.14) != QStringLiteral("4.6")
        || necwb::ui::formatAxisTick(5.3, 0.14) != QStringLiteral("5.3")) {
        return NECWB_SMOKE_FAILURE("display formatting");
    }
    QSettings{}.remove(QStringLiteral("smoke/overlayVisible"));
    auto overlayVisibilityChanged = false;
    auto overlayChooseRequested = false;
    NECWB_SMOKE_STEP();
    {
        necwb::ui::HistoricalOverlayControl overlay(QStringLiteral("smoke/overlayVisible"));
        overlay.setVisibilityChangedCallback([&overlayVisibilityChanged](bool visible) {
            overlayVisibilityChanged = visible;
        });
        overlay.setChooseRequestedCallback([&overlayChooseRequested](QWidget*) {
            overlayChooseRequested = true;
        });
        auto* showOverlay = overlay.findChild<QCheckBox*>(
            QStringLiteral("showHistoricalOverlay"));
        auto* chooseOverlay = overlay.findChild<QPushButton*>(
            QStringLiteral("chooseHistoricalOverlay"));
        if (showOverlay == nullptr || chooseOverlay == nullptr || showOverlay->isEnabled()
            || showOverlay->isChecked()) {
            return NECWB_SMOKE_FAILURE("historical overlay defaults");
        }
        overlay.setSelection(QStringLiteral("Run A"), QStringLiteral("Compatible"));
        showOverlay->click();
        chooseOverlay->click();
        if (!showOverlay->isEnabled() || !overlayVisibilityChanged
            || !overlayChooseRequested) {
            return NECWB_SMOKE_FAILURE("historical overlay controls");
        }
    }
    NECWB_SMOKE_STEP();
    {
        necwb::ui::HistoricalOverlayControl overlay(QStringLiteral("smoke/overlayVisible"));
        if (!overlay.isChecked())
            return NECWB_SMOKE_FAILURE("historical overlay visibility persistence");
    }
    QSettings{}.remove(QStringLiteral("smoke/overlayVisible"));
    // Settings that earlier layouts saved go once, gated by ui/layoutVersion; the single solver
    // path of early versions moves to the selected backend first.
    NECWB_SMOKE_STEP();
    {
        QSettings settings;
        const auto savedBackend = settings.value(QStringLiteral("analysis/backendId"));
        const auto savedPath = settings.value(QStringLiteral("analysis/executables/opennec"));
        settings.remove(QStringLiteral("ui/layoutVersion"));
        settings.remove(QStringLiteral("analysis/executables/opennec"));
        settings.setValue(QStringLiteral("analysis/backendId"), QStringLiteral("opennec"));
        settings.setValue(QStringLiteral("analysis/executablePath"), QStringLiteral("/opt/onec"));
        const QStringList retired{QStringLiteral("workspaceWindows/optimization/geometry"),
            QStringLiteral("resultWindows/impedance/visible"), QStringLiteral("mainWindow/geometry"),
            QStringLiteral("mainWindow/normalGeometry"), QStringLiteral("mainWindow/state"),
            QStringLiteral("modelCheckWindow/geometry"), QStringLiteral("runMonitorWindow/geometry")};
        for (const auto& key : retired) settings.setValue(key, 1);
        settings.setValue(QStringLiteral("mainWindow/v2/leftColumnWidth"), 420);
        necwb::ui::retireOldSettings();
        const auto retiredGone = std::ranges::none_of(retired,
            [&settings](const QString& key) { return settings.contains(key); });
        const auto cleaned = retiredGone && !settings.contains(QStringLiteral("analysis/executablePath"))
            && settings.value(QStringLiteral("analysis/executables/opennec")).toString()
                == QStringLiteral("/opt/onec")
            && settings.value(QStringLiteral("mainWindow/v2/leftColumnWidth")).toInt() == 420
            && settings.value(QStringLiteral("ui/layoutVersion")).toInt() == 2;
        settings.setValue(QStringLiteral("mainWindow/state"), 1);
        necwb::ui::retireOldSettings();
        const auto ranOnce = settings.contains(QStringLiteral("mainWindow/state"));
        settings.remove(QStringLiteral("mainWindow"));
        settings.setValue(QStringLiteral("analysis/backendId"), savedBackend);
        if (savedBackend.isNull()) settings.remove(QStringLiteral("analysis/backendId"));
        settings.setValue(QStringLiteral("analysis/executables/opennec"), savedPath);
        if (savedPath.isNull()) settings.remove(QStringLiteral("analysis/executables/opennec"));
        if (!cleaned || !ranOnce) return NECWB_SMOKE_FAILURE("retired settings removed once");
    }
    // Version 2 drops a solver path saved only because the PATH had it, so the bundled nec2c
    // is found first, and keeps the engine, every other path and a path that has gone.
    NECWB_SMOKE_STEP();
    {
        QSettings settings;
        const QStringList keys{QStringLiteral("analysis/backendId"),
            QStringLiteral("analysis/executables/nec2"), QStringLiteral("analysis/executables/opennec"),
            QStringLiteral("analysis/executables/nec2dxs")};
        std::vector<QVariant> saved;
        for (const auto& key : keys) saved.push_back(settings.value(key));
        const auto savedPath = qgetenv("PATH");
        QTemporaryDir bin;
        QFile fake(bin.filePath(QStringLiteral("nec2c") + ExecutableSuffix));
        fake.open(QIODevice::WriteOnly);
        fake.write("#!/bin/sh\n");
        fake.close();
        fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("PATH", QFile::encodeName(bin.path()));
        const auto migrate = [&settings](const QString& nec2) {
            settings.setValue(QStringLiteral("ui/layoutVersion"), 1);
            settings.setValue(QStringLiteral("analysis/executables/nec2"), nec2);
            necwb::ui::retireOldSettings();
        };
        settings.setValue(QStringLiteral("analysis/backendId"), QStringLiteral("nec2"));
        settings.setValue(QStringLiteral("analysis/executables/opennec"), QStringLiteral("/opt/onec"));
        settings.setValue(QStringLiteral("analysis/executables/nec2dxs"), QStringLiteral("C:/4nec2/Nec2dXS1k5.exe"));
        // The same file by another spelling counts as the one on the PATH.
        migrate(bin.path() + QStringLiteral("/./nec2c") + ExecutableSuffix);
        const auto foundDropped = !settings.contains(QStringLiteral("analysis/executables/nec2"))
            && settings.value(QStringLiteral("analysis/executables/opennec")).toString() == QStringLiteral("/opt/onec")
            && settings.value(QStringLiteral("analysis/executables/nec2dxs")).toString()
                == QStringLiteral("C:/4nec2/Nec2dXS1k5.exe")
            && settings.value(QStringLiteral("analysis/backendId")).toString() == QStringLiteral("nec2")
            && settings.value(QStringLiteral("ui/layoutVersion")).toInt() == 2;
        // Startup again leaves a path saved since; a path that has gone stays chosen.
        settings.setValue(QStringLiteral("analysis/executables/nec2"), fake.fileName());
        necwb::ui::retireOldSettings();
        const auto ranOnce = settings.value(QStringLiteral("analysis/executables/nec2")).toString() == fake.fileName();
        migrate(QStringLiteral("/gone/nec2c"));
        const auto missingKept = settings.value(QStringLiteral("analysis/executables/nec2")).toString()
            == QStringLiteral("/gone/nec2c");
        qputenv("PATH", savedPath);
        for (std::size_t index = 0; index < keys.size(); ++index) {
            if (saved[index].isValid()) settings.setValue(keys[index], saved[index]);
            else settings.remove(keys[index]);
        }
        if (!foundDropped || !ranOnce || !missingKept) {
            qWarning() << "solver path migration" << foundDropped << ranOnce << missingKept;
            return NECWB_SMOKE_FAILURE("found solver paths retired once");
        }
    }
    auto consolidatedSetupNavigation = false;
    auto linkedParameterDeleted = false;
    auto resultsColumnNavigation = false;
    auto bottomPanelNavigation = false;
    auto validationWindowNavigation = false;
    auto deckInCenter = false;
    auto geometryInMainWindow = false;
    auto undoFollowsFocusChanges = false;
    auto setupWindowNavigation = false;
    auto optimizationWindowNavigation = false;
    auto dialogDensityIsIncremental = false;
    QSettings resultWindowSettings;
    resultWindowSettings.remove(QStringLiteral("resultsColumn"));
    resultWindowSettings.remove(QStringLiteral("mainWindow/v2"));
    resultWindowSettings.remove(QStringLiteral("ui/bottomPanel"));
    resultWindowSettings.sync();
    NECWB_SMOKE_STEP();
    {
        necwb::ui::MainWindow mainWindow;
        mainWindow.setProperty("workspaceDensityId", QStringLiteral("density-smoke-sentinel"));
        auto* necDeckAction = mainWindow.findChild<QAction*>(QStringLiteral("necDeckAction"));
        auto* optimizeModuleAction = mainWindow.findChild<QAction*>(
            QStringLiteral("optimizeModuleAction"));
        auto* optimizationWorkspace = optimizerChild<QWidget>(
            QStringLiteral("optimizationWorkspace"));
        auto* validateMenu = mainWindow.findChild<QMenu*>(QStringLiteral("validateMenu"));
        auto* runMenu = mainWindow.findChild<QMenu*>(QStringLiteral("runMenu"));
        auto* recentModelsMenu = mainWindow.findChild<QMenu*>(
            QStringLiteral("recentModelsMenu"));
        auto* resultsWorkspace = mainWindow.findChild<QTabWidget*>(
            QStringLiteral("resultsWorkspace"));
        auto* bottomPanel = mainWindow.findChild<QWidget*>(QStringLiteral("bottomPanel"));
        auto* toggleBottomPanel = mainWindow.findChild<QAction*>(
            QStringLiteral("toggleBottomPanelAction"));
        auto* problemsScope = mainWindow.findChild<QLabel*>(QStringLiteral("problemsScope"));
        // One toolbar row and the model tree; the Project Tree dock and its toggle are gone.
        auto* toolBar = mainWindow.findChild<QToolBar*>(QStringLiteral("workbenchToolBar"));
        const auto compactPrimaryNavigation = mainWindow.findChild<QWidget*>(
            QStringLiteral("moduleNavigationToolbar")) == nullptr
            && mainWindow.findChild<QWidget*>(QStringLiteral("projectDock")) == nullptr
            && mainWindow.findChild<QWidget*>(QStringLiteral("modelTree")) != nullptr
            && toolBar != nullptr && !toolBar->isMovable()
            && toolBar->findChild<QComboBox*>(QStringLiteral("toolBarBackend")) != nullptr;
        auto* openValidation = mainWindow.findChild<QAction*>(
            QStringLiteral("showAverageGainResultAction"));
        auto* openConvergence = mainWindow.findChild<QAction*>(
            QStringLiteral("segmentationConvergenceAction"));
        auto* reviewRuns = mainWindow.findChild<QAction*>(
            QStringLiteral("reviewRunHistoryAction"));
        auto* toggleResultsColumn = mainWindow.findChild<QAction*>(
            QStringLiteral("toggleResultsColumnAction"));
        auto* showRawOutput = mainWindow.findChild<QAction*>(
            QStringLiteral("showRawOutputAction"));
        const auto overlays = mainWindow.findChildren<QCheckBox*>(
            QStringLiteral("showHistoricalOverlay"));
        // Setup pages and Variables live in the properties pane, the deck in the center.
        consolidatedSetupNavigation = mainWindow.findChild<QAction*>(
                QStringLiteral("setupModuleAction")) == nullptr
            && mainWindow.findChild<QTabWidget*>(QStringLiteral("setupWorkspace")) == nullptr
            && mainWindow.findChild<QTabWidget*>(QStringLiteral("modelWorkspace")) == nullptr
            && mainWindow.findChild<QAction*>(QStringLiteral("modelModuleAction")) == nullptr
            // The deck is raw source only; its cards are tables in the properties pane.
            && mainWindow.findChild<QTabWidget*>(QStringLiteral("necDeckWorkspace")) == nullptr
            && mainWindow.findChild<QWidget*>(QStringLiteral("cardsPanel")) == nullptr
            // Validation has no window or Overview of its own: the scope note heads Problems.
            && mainWindow.findChild<QTabWidget*>(QStringLiteral("validationWorkspace")) == nullptr
            && mainWindow.findChild<QWidget*>(QStringLiteral("validationOverview")) == nullptr
            && bottomPanel != nullptr && bottomPanel->isAncestorOf(problemsScope)
            && compactPrimaryNavigation
            && validateMenu != nullptr && runMenu != nullptr
            && recentModelsMenu != nullptr && !recentModelsMenu->actions().isEmpty()
            && openValidation != nullptr && validateMenu->actions().contains(openValidation)
            && reviewRuns != nullptr && runMenu->actions().contains(reviewRuns)
            && runMenu->actions().contains(mainWindow.findChild<QAction*>(
                QStringLiteral("openRunFolderAction")))
            // The main window is a normal resizable window, and run progress lives in
            // the bottom panel's Run log rather than a status bar widget.
            && mainWindow.layout()->sizeConstraint() != QLayout::SetFixedSize
            && mainWindow.maximumSize() == QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)
            && !mainWindow.statusBar()->isSizeGripEnabled()
            && mainWindow.findChild<QWidget*>(QStringLiteral("solverActivityWidget")) == nullptr;
        // Average Gain and Convergence open in the center under ← Geometry; Esc goes back.
        if (openValidation != nullptr && openConvergence != nullptr) {
            mainWindow.show();
            auto* split = mainWindow.findChild<QSplitter*>(QStringLiteral("workbenchSplit"));
            auto* geometry = mainWindow.findChild<QWidget*>(QStringLiteral("geometryWorkspace"));
            auto* averageGain = mainWindow.findChild<QWidget*>(QStringLiteral("averageGainView"));
            auto* convergence = mainWindow.findChild<QWidget*>(QStringLiteral("convergenceView"));
            const auto shownInCenter = [&](QWidget* view) {
                return view != nullptr && split != nullptr && split->isAncestorOf(view)
                    && view->isVisible() && geometry != nullptr && !geometry->isVisible();
            };
            openValidation->trigger();
            application.processEvents();
            const auto averageGainShown = shownInCenter(averageGain);
            if (auto* back = mainWindow.findChild<QToolButton*>(QStringLiteral("averageGainViewBack")))
                back->click();
            application.processEvents();
            const auto averageGainBack = geometry->isVisible() && !averageGain->isVisible();
            openConvergence->setEnabled(true);
            openConvergence->trigger();
            application.processEvents();
            const auto convergenceShown = shownInCenter(convergence);
            QTest::keyClick(convergence, Qt::Key_Escape);
            application.processEvents();
            validationWindowNavigation = averageGainShown && averageGainBack && convergenceShown
                && geometry->isVisible() && !convergence->isVisible()
                && topLevelDialog(QStringLiteral("validationWindow")) == nullptr;
            if (!validationWindowNavigation)
                qWarning() << "validation in center" << averageGainShown << averageGainBack
                           << convergenceShown;
        }
        // The NEC Deck opens in the center in place of the geometry; ← Geometry returns.
        auto* deckWorkspace = mainWindow.findChild<QWidget*>(QStringLiteral("necSourceEditor"));
        if (deckWorkspace != nullptr && necDeckAction != nullptr) {
            mainWindow.show();
            necDeckAction->setEnabled(true);
            necDeckAction->trigger();
            application.processEvents();
            auto* split = mainWindow.findChild<QWidget*>(QStringLiteral("workbenchSplit"));
            auto* geometryWorkspace = mainWindow.findChild<QWidget*>(
                QStringLiteral("geometryWorkspace"));
            const auto deckShown = deckWorkspace->window() == &mainWindow && split != nullptr
                && split->isAncestorOf(deckWorkspace) && deckWorkspace->isVisible()
                && geometryWorkspace != nullptr && !geometryWorkspace->isVisible()
                && topLevelDialog(QStringLiteral("modelEditorWindow")) == nullptr;
            if (auto* back = mainWindow.findChild<QToolButton*>(QStringLiteral("necDeckViewBack")))
                back->click();
            application.processEvents();
            deckInCenter = deckShown && !deckWorkspace->isVisible()
                && geometryWorkspace->isVisible();
            // Geometry lives in the main window's center, not in a window of its own.
            auto* geometryEditorAction = mainWindow.findChild<QAction*>(
                QStringLiteral("geometryEditorAction"));
            if (geometryEditorAction != nullptr) {
                necDeckAction->trigger();
                geometryEditorAction->trigger();
                application.processEvents();
                geometryInMainWindow = geometryEditorAction->isEnabled()
                    && geometryWorkspace->window() == &mainWindow
                    && split->isAncestorOf(geometryWorkspace)
                    && geometryWorkspace->isVisibleTo(&mainWindow)
                    && topLevelDialog(QStringLiteral("geometryEditorWindow")) == nullptr;
            }
        }
        // The Frequency row shows its page in the pane, inside the main window.
        if (auto* tree = mainWindow.findChild<QTreeWidget*>(QStringLiteral("modelTree"))) {
            mainWindow.show();
            application.processEvents();
            tree->setCurrentItem(tree->findItems(QStringLiteral("Frequency"), Qt::MatchExactly).value(0));
            application.processEvents();
            const auto* frequencyPage = mainWindow.findChild<QWidget*>(
                QStringLiteral("frequencyEditorPage"));
            const auto* pane = mainWindow.findChild<QWidget*>(QStringLiteral("propertiesPane"));
            // F5 and F7 stay window shortcuts that no field in the column keeps for itself.
            const auto leavesKey = [](QWidget* widget, Qt::Key key) {
                if (widget == nullptr) return false;
                QKeyEvent override(QEvent::ShortcutOverride, key, Qt::NoModifier);
                override.ignore();
                QApplication::sendEvent(widget, &override);
                return !override.isAccepted();
            };
            auto* paneField = frequencyPage == nullptr ? nullptr
                : frequencyPage->findChild<QDoubleSpinBox*>();
            QAction* runShortcut = nullptr;
            QAction* checkShortcut = nullptr;
            for (auto* action : mainWindow.findChildren<QAction*>()) {
                if (action->shortcut() == QKeySequence(Qt::Key_F5)) runShortcut = action;
                if (action->shortcut() == QKeySequence(Qt::Key_F7)) checkShortcut = action;
            }
            const auto functionKeysReachWindow = runShortcut != nullptr && checkShortcut != nullptr
                && runShortcut->shortcutContext() == Qt::WindowShortcut
                && checkShortcut->shortcutContext() == Qt::WindowShortcut
                && leavesKey(tree, Qt::Key_F5) && leavesKey(tree, Qt::Key_F7)
                && leavesKey(paneField, Qt::Key_F5) && leavesKey(paneField, Qt::Key_F7);
            setupWindowNavigation = frequencyPage != nullptr && pane != nullptr
                && frequencyPage->window() == &mainWindow && pane->isAncestorOf(frequencyPage)
                && frequencyPage->isVisible() && functionKeysReachWindow
                && topLevelDialog(QStringLiteral("analysisSetupWindow")) == nullptr;
        }
        // Optimize opens its own top-level window beside the main window, setup and results
        // side by side; a second Optimize raises the same window, closing it keeps the study,
        // its place is remembered, and closing the main window closes it.
        if (optimizationWorkspace != nullptr && optimizeModuleAction != nullptr) {
            QSettings{}.remove(QStringLiteral("optimizerWindow"));
            mainWindow.resize(1400, 900);
            mainWindow.show();
            optimizeModuleAction->setEnabled(true);
            optimizeModuleAction->trigger();
            application.processEvents();
            auto* window = optimizationWorkspace->window();
            auto* geometry = mainWindow.findChild<QWidget*>(QStringLiteral("geometryWorkspace"));
            auto* method = optimizationWorkspace->findChild<QComboBox*>(
                QStringLiteral("optimizationSearchMethod"));
            auto* stack = optimizationWorkspace->findChild<QSplitter*>(
                QStringLiteral("optimizationWorkspaceSplitter"));
            const auto windows = std::ranges::count_if(QApplication::topLevelWidgets(),
                [](const QWidget* widget) { return widget->objectName() == QStringLiteral("optimizerWindow"); });
            const auto ownWindow = window != &mainWindow && window->parentWidget() == nullptr
                && window->objectName() == QStringLiteral("optimizerWindow") && window->isVisible()
                && windows == 1 && geometry != nullptr && geometry->isVisible()
                && window->windowTitle().startsWith(QStringLiteral("Optimize · "))
                && mainWindow.findChild<QWidget*>(QStringLiteral("optimizationView")) == nullptr;
            window->resize(1120, 720);
            application.processEvents();
            const auto sideBySide = stack != nullptr && stack->orientation() == Qt::Horizontal;
            window->resize(520, 720);
            application.processEvents();
            const auto stacksNarrow = stack != nullptr && stack->orientation() == Qt::Vertical;
            // The offscreen screen is small; a size and place inside it come back as left.
            window->resize(640, 480);
            if (method != nullptr) method->setCurrentIndex(2);
            window->move(40, 30);
            application.processEvents();
            const auto placed = window->pos();
            window->close();
            application.processEvents();
            const auto closedHidden = !window->isVisible() && !QSettings{}.value(
                QStringLiteral("optimizerWindow/geometry")).toByteArray().isEmpty();
            optimizeModuleAction->trigger();
            optimizeModuleAction->trigger();
            application.processEvents();
            const auto keptState = window->isVisible() && optimizationWorkspace->window() == window
                && method != nullptr && method->currentIndex() == 2 && window->pos() == placed
                && window->size() == QSize(640, 480);
            mainWindow.close();
            application.processEvents();
            const auto closesWithMain = !window->isVisible();
            mainWindow.show();
            application.processEvents();
            optimizationWindowNavigation = ownWindow && sideBySide && stacksNarrow && closedHidden
                && keptState && closesWithMain;
            if (!optimizationWindowNavigation)
                qWarning() << "optimizer window" << ownWindow << windows << sideBySide << stacksNarrow
                           << closedHidden << keptState << placed << window->pos() << window->size()
                           << closesWithMain;
        }
        // Dialogs follow the workspace density when they are shown.
        {
            QDialog probe(&mainWindow);
            probe.show();
            application.processEvents();
            dialogDensityIsIncremental = !probe.property("workspaceDensityId").toString().isEmpty()
                && mainWindow.property("workspaceDensityId").toString()
                    == QStringLiteral("density-smoke-sentinel");
        }
        // Results live in the main window's right column: no result windows, no Result
        // Windows menu, one overlay control, and View › Results Column collapses the column.
        auto* split = mainWindow.findChild<QSplitter*>(QStringLiteral("workbenchSplit"));
        auto* resultsColumn = mainWindow.findChild<QWidget*>(QStringLiteral("resultsColumn"));
        if (toggleResultsColumn != nullptr && split != nullptr && resultsColumn != nullptr) {
            mainWindow.resize(1400, 900);
            mainWindow.show();
            application.processEvents();
            const auto* empty = resultsColumn->findChild<QLabel*>(
                QStringLiteral("resultsColumnEmpty"));
            const auto* thumbnail = resultsColumn->findChild<QWidget*>(
                QStringLiteral("swrThumbnail"));
            const auto columnShown = split->indexOf(resultsColumn) == 2
                && split->sizes().value(2) > 0 && toggleResultsColumn->isChecked()
                && empty != nullptr && empty->isVisible() && thumbnail != nullptr
                && !thumbnail->isVisible();
            toggleResultsColumn->trigger();
            application.processEvents();
            const auto columnCollapsed = split->sizes().value(2) == 0
                && resultWindowSettings.value(
                    QStringLiteral("mainWindow/v2/rightColumnCollapsed")).toBool();
            toggleResultsColumn->trigger();
            application.processEvents();
            resultsColumnNavigation = columnShown && columnCollapsed
                && split->sizes().value(2) >= resultsColumn->minimumSizeHint().width()
                && mainWindow.findChild<QMenu*>(QStringLiteral("resultWindowsMenu")) == nullptr
                && topLevelDialog(QStringLiteral("impedanceResultWindow")) == nullptr
                && topLevelDialog(QStringLiteral("currentsResultWindow")) == nullptr
                && topLevelDialog(QStringLiteral("radiationResultWindow")) == nullptr
                && overlays.size() == 1 && !overlays.front()->isChecked()
                && resultsWorkspace == nullptr;
        }
        // Run history and raw output open as tabs of the bottom panel inside the main
        // window; the panel starts collapsed, opens inside the window without resizing it,
        // and its View toggle collapses it again.
        if (bottomPanel != nullptr && toggleBottomPanel != nullptr && reviewRuns != nullptr
            && showRawOutput != nullptr) {
            const auto startedCollapsed = !toggleBottomPanel->isChecked();
            const auto windowSize = mainWindow.size();
            const auto collapsedHeight = bottomPanel->sizeHint().height();
            reviewRuns->trigger();
            application.processEvents();
            const auto* runsTable = bottomPanel->findChild<QTableWidget*>(
                QStringLiteral("analysisRunsTable"));
            const auto runsShown = runsTable != nullptr && runsTable->isVisibleTo(&mainWindow)
                && runsTable->window() == &mainWindow && toggleBottomPanel->isChecked()
                && bottomPanel->sizeHint().height() > collapsedHeight
                && mainWindow.size() == windowSize;
            showRawOutput->trigger();
            application.processEvents();
            const auto* rawOutput = bottomPanel->findChild<QPlainTextEdit*>(
                QStringLiteral("analysisRawOutput"));
            const auto rawOutputShown = rawOutput != nullptr && rawOutput->isVisibleTo(&mainWindow)
                && runsTable != nullptr && !runsTable->isVisibleTo(&mainWindow);
            toggleBottomPanel->trigger();
            application.processEvents();
            bottomPanelNavigation = startedCollapsed && runsShown && rawOutputShown
                && rawOutput != nullptr && !rawOutput->isVisibleTo(&mainWindow)
                && bottomPanel->sizeHint().height() == collapsedHeight
                && mainWindow.size() == windowSize
                && topLevelDialog(QStringLiteral("runHistoryWindow")) == nullptr
                && topLevelDialog(QStringLiteral("rawOutputWindow")) == nullptr;
        }
        // A parameter linked into a card field (as the optimizer does) can still be
        // deleted: confirming writes its current value back into the field.
        QPlainTextEdit* deckEditor = nullptr;
        QTableWidget* parameterTable = nullptr;
        QPushButton* deleteParameter = nullptr;
        for (auto* window : QApplication::topLevelWidgets()) {
            if (deckEditor == nullptr)
                deckEditor = window->findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor"));
            if (parameterTable == nullptr)
                parameterTable = window->findChild<QTableWidget*>(QStringLiteral("parameterTable"));
            if (deleteParameter == nullptr)
                deleteParameter = window->findChild<QPushButton*>(
                    QStringLiteral("deleteParameterButton"));
        }
        QAction* checkModelAction = nullptr;
        for (auto* action : mainWindow.findChildren<QAction*>())
            if (action->text() == QStringLiteral("&Check Model")) checkModelAction = action;
        if (deckEditor != nullptr && parameterTable != nullptr && deleteParameter != nullptr
            && checkModelAction != nullptr) {
            deckEditor->setPlainText(QStringLiteral(
                "SY wire_x1=5\nGW 1 11 -wire_x1 0 6 wire_x1 0 6 0.001\nGE 0\nEN\n"));
            checkModelAction->trigger();
            application.processEvents();
            parameterTable->selectRow(0);
                QTimer::singleShot(0, [] {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    box->button(QMessageBox::Yes)->click();
            });
            deleteParameter->click();
            application.processEvents();
                linkedParameterDeleted = deckEditor->toPlainText()
                == QStringLiteral("GW 1 11 -5 0 6 5 0 6 0.001\nGE 0\nEN\n");
            // Undo and Redo follow the shared source history whether the geometry view or
            // the NEC Deck is in the center.
            QAction* undo = nullptr;
            QAction* redo = nullptr;
            QAction* geometryEditor = nullptr;
            for (auto* action : mainWindow.findChildren<QAction*>()) {
                if (action->text().startsWith(QStringLiteral("&Undo"))) undo = action;
                if (action->text().startsWith(QStringLiteral("&Redo"))) redo = action;
                if (action->objectName() == QStringLiteral("geometryEditorAction"))
                    geometryEditor = action;
            }
            auto* modelModule = mainWindow.findChild<QAction*>(QStringLiteral("necDeckAction"));
            if (undo != nullptr && redo != nullptr && geometryEditor != nullptr
                && modelModule != nullptr) {
                geometryEditor->trigger();
                application.processEvents();
                const auto undoInGeometry = undo->isEnabled() && !redo->isEnabled();
                modelModule->trigger();
                application.processEvents();
                const auto undoInDeck = undo->isEnabled() && !redo->isEnabled();
                undo->trigger();
                application.processEvents();
                const auto redoInDeck = redo->isEnabled();
                geometryEditor->trigger();
                application.processEvents();
                undoFollowsFocusChanges = undoInGeometry && undoInDeck && redoInDeck
                    && redo->isEnabled() && redo->text() != QStringLiteral("&Redo")
                    && deckEditor->toPlainText().startsWith(QStringLiteral("SY wire_x1=5"));
            }
        }
    }
    resultWindowSettings.remove(QStringLiteral("resultsColumn"));
    resultWindowSettings.remove(QStringLiteral("ui/bottomPanel"));
    resultWindowSettings.sync();
    // Launch: with no model the workspace shows the start panel in its center; reopening the
    // last model obeys the setting, Shift, a missing file and an attempt that never finished.
    NECWB_SMOKE_STEP();
    {
        QSettings launchSettings;
        const QStringList launchKeys{QStringLiteral("files/recentModels"),
            QStringLiteral("files/recentOpened"), QStringLiteral("files/reopenLastModel"),
            QStringLiteral("files/reopening")};
        std::vector<QVariant> savedLaunch;
        for (const auto& key : launchKeys) savedLaunch.push_back(launchSettings.value(key));
        QTemporaryDir launchDirectory;
        const auto lastPath = launchDirectory.filePath(QStringLiteral("last.nec"));
        QFile lastFile(lastPath);
        if (lastFile.open(QIODevice::WriteOnly)) lastFile.write("GW 1 11 -5 0 6 5 0 6 0.001\nGE 0\nEN\n");
        lastFile.close();
        const auto missingPath = launchDirectory.filePath(QStringLiteral("gone.nec"));
        const auto launch = [&](const QStringList& recent, bool reopen, const QString& pending,
                                bool shift) {
            launchSettings.setValue(QStringLiteral("files/recentModels"), recent);
            necwb::ui::recentModels::setReopenAtStartup(reopen);
            necwb::ui::recentModels::setPendingReopen(pending);
            auto window = std::make_unique<necwb::ui::MainWindow>();
            window->resize(1400, 900);
            window->show();
            application.processEvents();
            window->reopenLastModel(shift);
            application.processEvents();
            return window;
        };
        const auto startShown = [](necwb::ui::MainWindow& window) {
            auto* start = window.findChild<QWidget*>(QStringLiteral("startPanel"));
            auto* split = window.findChild<QWidget*>(QStringLiteral("workbenchSplit"));
            return start != nullptr && start->isVisible() && split != nullptr && split->isVisible();
        };
        const auto stale = launch({lastPath}, true, lastPath, false);
        const auto staleSkipped = startShown(*stale)
            && stale->statusBar()->currentMessage() == QStringLiteral(
                "Skipped reopening %1: it did not finish opening last time")
                .arg(QDir::toNativeSeparators(lastPath))
            && necwb::ui::recentModels::pendingReopen().isEmpty();
        const auto missing = launch({missingPath}, true, {}, false);
        const auto missingReported = startShown(*missing)
            && missing->statusBar()->currentMessage()
                == QStringLiteral("Last model not found: %1").arg(QDir::toNativeSeparators(missingPath));
        const auto settingOff = launch({lastPath}, false, {}, false);
        const auto shiftHeld = launch({lastPath}, true, {}, true);
        const auto skipped = startShown(*settingOff) && startShown(*shiftHeld);
        const auto reopened = launch({lastPath}, true, {}, false);
        auto* reopenedGeometry = reopened->findChild<QWidget*>(QStringLiteral("geometryWorkspace"));
        const auto lastReopened = !startShown(*reopened) && reopenedGeometry != nullptr
            && reopenedGeometry->isVisible() && reopened->windowTitle().startsWith(QStringLiteral("last.nec"))
            && necwb::ui::recentModels::pendingReopen().isEmpty();
        // The reopened model's views fit once the window has its final size, and keep fitting
        // through resizes until the user zooms one.
        reopened->resize(1600, 1000);
        application.processEvents();
        necwb::ui::GeometryView* plane = nullptr;
        for (auto* candidate : reopened->findChildren<necwb::ui::GeometryView*>())
            if (plane == nullptr && candidate->isVisible()) plane = candidate;
        const auto fittedNow = [&] {
            const auto shown = plane->grab().toImage();
            plane->fitToView();
            return shown == plane->grab().toImage();
        };
        const auto fittedAfterResize = plane != nullptr && fittedNow();
        auto zoomStaysAfterResize = false;
        if (plane != nullptr) {
            QWheelEvent zoom(QPointF(plane->rect().center()), plane->mapToGlobal(QPointF(plane->rect().center())),
                {}, {0, 240}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(plane, &zoom);
            reopened->resize(1500, 950);
            application.processEvents();
            zoomStaysAfterResize = !fittedNow();
        }
        const auto popupShortcut = std::ranges::count_if(reopened->findChildren<QAction*>(),
            [](const QAction* action) {
                return action->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
            }) == 1 && reopened->findChild<QAction*>(QStringLiteral("openRecentPopupAction")) != nullptr;
        for (std::size_t index = 0; index < launchKeys.size(); ++index) {
            if (savedLaunch[index].isValid()) launchSettings.setValue(launchKeys[index], savedLaunch[index]);
            else launchSettings.remove(launchKeys[index]);
        }
        if (!staleSkipped || !missingReported || !skipped || !lastReopened || !popupShortcut
            || !fittedAfterResize || !zoomStaysAfterResize) {
            qWarning() << "launch" << staleSkipped << missingReported << skipped << lastReopened
                       << popupShortcut << fittedAfterResize << zoomStaysAfterResize << stale->statusBar()->currentMessage()
                       << missing->statusBar()->currentMessage() << reopened->windowTitle();
            return NECWB_SMOKE_FAILURE("launch start panel and reopen");
        }
    }
    // The delta loop lies in XZ, so XZ opens in the big view; double-clicking XY's title swaps
    // it in, fitted, and edits keep it there until a model is opened again. The wire form
    // shows deck units (meters) with Geometry in feet, and Make Variable names W1_X1.
    NECWB_SMOKE_STEP();
    {
        QSettings planeSettings;
        const QStringList planeKeys{QStringLiteral("files/recentModels"),
            QStringLiteral("files/recentOpened"), QStringLiteral("files/reopenLastModel")};
        std::vector<QVariant> savedPlane;
        for (const auto& key : planeKeys) savedPlane.push_back(planeSettings.value(key));
        QTemporaryDir planeDirectory;
        const auto deltaPath = planeDirectory.filePath(QStringLiteral("delta.nec"));
        QFile delta(deltaPath);
        if (delta.open(QIODevice::WriteOnly)) {
            delta.write("CE\nGW 1,21,-1.87,0.,-1.87,1.87,0.,-1.87,.000814\n"
                "GW 2,21,1.87,0.,-1.87,1.87,0.,1.87,.000814\n"
                "GW 3,21,1.87,0.,1.87,-1.87,0.,1.87,.000814\n"
                "GW 4,21,-1.87,0.,1.87,-1.87,0.,-1.87,.000814\nGE 0\nFR 0,1,0,0,21.15\nEX 0,1,11,0,1,0\nEN\n");
        }
        delta.close();
        planeSettings.setValue(QStringLiteral("files/recentModels"), QStringList{deltaPath});
        necwb::ui::recentModels::setReopenAtStartup(true);
        necwb::ui::MainWindow window;
        window.resize(1400, 900);
        window.show();
        application.processEvents();
        window.reopenLastModel(false);
        application.processEvents();
        using Plane = necwb::geometry::ProjectionPlane;
        auto* planes = dynamic_cast<necwb::ui::GeometryPlanes*>(
            window.findChild<QSplitter*>(QStringLiteral("geometryPlanes")));
        auto* deck = window.findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor"));
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        if (planes == nullptr || deck == nullptr || tree == nullptr || undo == nullptr)
            return NECWB_SMOKE_FAILURE("geometry planes controls");
        const auto widestFirst = planes->mainPlane() == Plane::XZ;
        QWidget* xyHeader = nullptr;
        for (auto* header : planes->findChildren<QWidget*>(QStringLiteral("geometryPaneHeader")))
            for (const auto* label : header->findChildren<QLabel*>())
                if (label->text() == QStringLiteral("XY Top")) xyHeader = header;
        if (xyHeader != nullptr) {
            QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(5, 5),
                xyHeader->mapToGlobal(QPointF(5, 5)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(xyHeader, &doubleClick);
        }
        application.processEvents();
        const auto fitted = [](necwb::ui::GeometryView* view) {
            const auto shown = view->grab().toImage();
            view->fitToView();
            return shown == view->grab().toImage();
        };
        const auto views = planes->views();
        const auto swapped = planes->mainPlane() == Plane::XY && fitted(views[0]) && fitted(views[1]);
        auto keptAfterEdit = false;

        // Units: Geometry in feet; the Wires table stays in the deck's meters.
        if (auto* units = window.findChild<QComboBox*>(QStringLiteral("geometryLengthUnit")))
            units->setCurrentIndex(units->findText(QStringLiteral("ft")));
        application.processEvents();
        const auto clickTree = [&](const QString& name) {
            auto* item = tree->findItems(name, Qt::MatchExactly | Qt::MatchRecursive).value(0);
            if (item != nullptr) {
                tree->setCurrentItem(item);
                emit tree->itemClicked(item, 0);
            }
            application.processEvents();
        };
        // A wire picked in the tree is its row in the Wires table.
        clickTree(QStringLiteral("Wire 1"));
        auto* wireCells = window.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
        const auto tableInDeckUnits = wireCells != nullptr && wireCells->isVisible()
            && wireCells->currentRow() == 0 && wireCells->item(0, 2)->text() == QStringLiteral("-1.87")
            && wireCells->horizontalHeaderItem(2)->text() == QStringLiteral("X1 m")
            && wireCells->horizontalHeaderItem(8)->text() == QStringLiteral("Radius m")
            && wireCells->toolTip().startsWith(
                QString::fromUtf8("Right-click a cell → Make Variable…, or type a name in the cell."));

        // Make Variable from the Wires table opens the Make Variable dialog, whose defaults
        // create a new variable for the value.
        const auto makeVariable = [&](QWidget* target, QPoint position, const QString& expected) {
            const auto deckBefore = deck->toPlainText();
            QString dialogTitle;
            QTimer::singleShot(50, [&dialogTitle] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (menu == nullptr) return;
                for (auto* action : menu->actions())
                    if (action->text() == QString::fromUtf8("Make Variable…")) {
                        QTimer::singleShot(100, [&dialogTitle] {
                            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                                dialogTitle = dialog->windowTitle();
                                dialog->accept();
                            }
                        });
                        menu->setActiveAction(action);
                        QTest::keyClick(menu, Qt::Key_Return);
                    }
            });
            emit target->customContextMenuRequested(position);
            QElapsedTimer made;
            made.start();
            while (deck->toPlainText() == deckBefore && made.elapsed() < 2000)
                application.processEvents(QEventLoop::AllEvents, 50);
            const auto written = dialogTitle == QStringLiteral("Make Variable")
                && deck->toPlainText().contains(expected);
            // Making a variable edits the same model, so the chosen main view stays.
            keptAfterEdit = planes->mainPlane() == Plane::XY;
            undo->trigger();
            application.processEvents();
            if (!written || deck->toPlainText() != deckBefore)
                qWarning() << "make variable" << dialogTitle << deck->toPlainText().left(120);
            return written && deck->toPlainText() == deckBefore;
        };
        const auto madeVariable = wireCells != nullptr && makeVariable(wireCells,
            wireCells->visualItemRect(wireCells->item(0, 2)).center(), QStringLiteral("SY gw_x1=1.87"));

        // Opening a model again picks its widest plane.
        deck->document()->setModified(false);
        auto* recentMenu = window.findChild<QMenu*>(QStringLiteral("recentModelsMenu"));
        QAction* reopenDelta = nullptr;
        for (auto* action : recentMenu != nullptr ? recentMenu->actions() : QList<QAction*>{})
            if (action->data().toString() == QFileInfo(deltaPath).absoluteFilePath()) reopenDelta = action;
        // Opening rebuilds the menu, so the action is triggered after the search.
        if (reopenDelta != nullptr) reopenDelta->trigger();
        application.processEvents();
        const auto choseAgain = planes->mainPlane() == Plane::XZ;
        // Units stays on the geometry toolbar at 1280 px with the left column at its width;
        // narrower, an item that overflows takes its label with it.
        auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("geometryToolbar"));
        auto* unitsItem = window.findChild<QWidget*>(QStringLiteral("geometryUnitsItem"));
        const auto onToolbar = [toolbar](QWidget* item) {
            return item->isVisible() && toolbar->rect().contains(QRect(item->mapTo(toolbar, QPoint(0, 0)), item->size()));
        };
        const auto noStrandedLabel = [toolbar] {
            for (auto* label : toolbar->findChildren<QLabel*>()) {
                if (!label->isVisible()) continue;
                for (auto* sibling : label->parentWidget()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                    if (sibling != label && !sibling->isVisible()) return false;
                if (label->parentWidget() == toolbar) return false;
            }
            return true;
        };
        window.resize(1280, 900);
        application.processEvents();
        const auto unitsAt1280 = toolbar != nullptr && unitsItem != nullptr && onToolbar(unitsItem)
            && noStrandedLabel();
        window.resize(window.minimumSizeHint().width(), 900);
        application.processEvents();
        const auto narrowKeepsLabels = toolbar != nullptr && noStrandedLabel();
        // What overflows is in the ›› menu as working controls, kept in step with the toolbar's.
        // The toolbar is narrowed to its minimum, Snap and ››, so Units overflows whatever the
        // fonts; at the window's minimum it may still fit.
        toolbar->setMaximumWidth(toolbar->minimumSizeHint().width());
        application.processEvents();
        const auto inOverflow = [&](const std::function<void(QMenu*)>& use) {
            auto* extension = toolbar->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
            if (extension == nullptr || !extension->isVisible() || extension->menu() == nullptr)
                return false;
            auto opened = false;
            QTimer::singleShot(50, [&] {
                auto* menu = extension->menu();
                opened = menu->isVisible();
                if (opened) use(menu);
                menu->close();
            });
            extension->click();
            application.processEvents();
            return opened;
        };
        const auto overflowCopy = [](QMenu* menu, const QString& name) -> QWidget* {
            auto* copy = menu->findChild<QWidget*>(name + QStringLiteral("Overflow"));
            return copy != nullptr && copy->isVisible() ? copy : nullptr;
        };
        auto* mainUnits = window.findChild<QComboBox*>(QStringLiteral("geometryLengthUnit"));
        auto* mainSpacing = window.findChild<QDoubleSpinBox*>(QStringLiteral("geometrySnapSpacing"));
        auto unitsInMenu = false;
        const auto openedNarrow = inOverflow([&](QMenu* menu) {
            auto* units = qobject_cast<QComboBox*>(overflowCopy(menu, QStringLiteral("geometryLengthUnit")));
            unitsInMenu = units != nullptr && units->currentText() == QStringLiteral("ft")
                && overflowCopy(menu, QStringLiteral("geometryUnitsItem")) != nullptr
                && !unitsItem->isVisible();
            if (units != nullptr) units->setCurrentIndex(units->findText(QStringLiteral("cm")));
        });
        const auto overflowUnitApplied = mainUnits->currentText() == QStringLiteral("cm")
            && mainSpacing->suffix() == QStringLiteral(" cm");
        // A change on the toolbar shows in the menu's copy. Snap, the first item, stays on the
        // toolbar however narrow it is.
        mainUnits->setCurrentIndex(mainUnits->findText(QStringLiteral("ft")));
        application.processEvents();
        auto toolbarUnitShown = false;
        const auto openedAgain = inOverflow([&](QMenu* menu) {
            auto* units = qobject_cast<QComboBox*>(overflowCopy(menu, QStringLiteral("geometryLengthUnit")));
            toolbarUnitShown = units != nullptr && units->currentText() == QStringLiteral("ft");
        });
        toolbar->setMaximumWidth(80);
        application.processEvents();
        const auto snapStays = mainSpacing->isVisible()
            && window.findChild<QToolButton*>(QStringLiteral("geometrySnapButton"))->isVisible();
        toolbar->setMaximumWidth(QWIDGETSIZE_MAX);
        const auto overflowWorks = openedNarrow && unitsInMenu && overflowUnitApplied
            && openedAgain && toolbarUnitShown && snapStays;
        if (!unitsAt1280 || !narrowKeepsLabels || !overflowWorks)
            qWarning() << "units toolbar" << unitsAt1280 << narrowKeepsLabels << window.width()
                       << (toolbar ? toolbar->width() : -1) << openedNarrow << unitsInMenu
                       << overflowUnitApplied << openedAgain << toolbarUnitShown << snapStays;
        window.resize(1400, 900);
        application.processEvents();

        // Top, Front, Side, 3D and Fit bring the geometry forward from the Deck and the full
        // results view, keeping the 2D or 3D page.
        auto* layout = dynamic_cast<necwb::ui::WorkbenchLayout*>(window.centralWidget());
        auto* workspace = window.findChild<QWidget*>(QStringLiteral("geometryWorkspace"));
        auto* geometryPage = qobject_cast<necwb::ui::GeometryWorkspace*>(workspace);
        const auto trigger = [&](const char* name) {
            if (auto* action = window.findChild<QAction*>(QString::fromLatin1(name))) action->trigger();
            application.processEvents();
        };
        const auto shows = [&](int page) {
            return workspace->isVisible() && geometryPage->page() == page;
        };
        using Page = necwb::ui::GeometryWorkspace::Page;
        trigger("necDeckAction");
        const auto deckShown = !workspace->isVisible();
        trigger("geometry3DView");
        const auto threeDFromDeck = deckShown && shows(Page::ThreeD);
        trigger("necDeckAction");
        trigger("geometryFit");
        const auto fitFromDeckKeeps3D = shows(Page::ThreeD);
        auto* fullView = window.findChild<QWidget*>(QStringLiteral("resultFullView"));
        layout->showView(fullView);
        application.processEvents();
        const auto fullShown = layout->isShown(fullView) && !workspace->isVisible();
        trigger("geometryFrontView");
        const auto frontFromFull = fullShown && shows(Page::Planes)
            && planes->mainPlane() == Plane::XZ;
        layout->showView(fullView);
        application.processEvents();
        trigger("geometryFit");
        const auto fitFromFullKeepsPlanes = shows(Page::Planes) && planes->mainPlane() == Plane::XZ;
        trigger("necDeckAction");
        trigger("geometrySideView");
        const auto sideFromDeck = shows(Page::Planes) && planes->mainPlane() == Plane::YZ;
        const auto broughtForward = threeDFromDeck && fitFromDeckKeeps3D && frontFromFull
            && fitFromFullKeepsPlanes && sideFromDeck;
        if (!broughtForward)
            qWarning() << "view actions bring geometry forward" << deckShown << threeDFromDeck
                       << fitFromDeckKeeps3D << fullShown << frontFromFull
                       << fitFromFullKeepsPlanes << sideFromDeck;
        for (std::size_t index = 0; index < planeKeys.size(); ++index) {
            if (savedPlane[index].isValid()) planeSettings.setValue(planeKeys[index], savedPlane[index]);
            else planeSettings.remove(planeKeys[index]);
        }
        if (!widestFirst || !swapped || !keptAfterEdit || !tableInDeckUnits
            || !madeVariable || !choseAgain || !unitsAt1280 || !narrowKeepsLabels
            || !overflowWorks || !broughtForward) {
            qWarning() << "planes/units/variables" << widestFirst << swapped << keptAfterEdit
                       << tableInDeckUnits
                       << (wireCells ? wireCells->horizontalHeaderItem(2)->text() : QString{})
                       << madeVariable << choseAgain;
            return NECWB_SMOKE_FAILURE("geometry planes, deck units and Make Variable");
        }
    }
    // The geometry views: nothing drawn over a plane but the model, distinct axis labels on a
    // plane that sees the model end-on, and wires coloured by the Currents view's values.
    NECWB_SMOKE_STEP();
    {
        QSettings{}.setValue(QStringLiteral("geometry/showCurrents"), true);
        necwb::model::AntennaModel pair;
        pair.addWire({1, {-2.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, 4, 0.001, 1});
        pair.addWire({2, {-2.0, 1.5, 0.0}, {2.0, 1.5, 0.0}, 4, 0.001, 2});
        QWidget host;
        auto* hostLayout = new QHBoxLayout(&host);
        auto* workspace = new necwb::ui::GeometryWorkspace(&host);
        hostLayout->addWidget(workspace);
        host.resize(1100, 760);
        host.show();
        application.processEvents();
        for (auto* view : workspace->views()) view->setModel(pair);
        application.processEvents();
        auto* xy = workspace->planes()->views()[0];
        auto* xz = workspace->planes()->views()[1];

        // No widget over any view: the title and info lines sit above and below it.
        const auto clearOver = [&] {
            for (auto* view : workspace->views()) {
                if (!view->isVisible()) continue;
                const QRect area(view->mapToGlobal(QPoint(0, 0)), view->size());
                for (auto* other : workspace->findChildren<QWidget*>()) {
                    if (!other->isVisible() || other == view || other->isAncestorOf(view)) continue;
                    // A splitter handle is between the panes, though rounding at a fractional
                    // pixel ratio can make it touch one.
                    if (qobject_cast<QSplitterHandle*>(other) != nullptr) continue;
                    if (QRect(other->mapToGlobal(QPoint(0, 0)), other->size()).intersects(area))
                        return false;
                }
            }
            return true;
        };
        auto* xyInfo = workspace->planes()->panes()[0]->info();
        const auto infoUnder = xyInfo->isVisible()
            && xyInfo->mapTo(workspace, QPoint(0, 0)).y() >= xy->mapTo(workspace, QPoint(0, xy->height())).y()
            && xyInfo->text().contains(QStringLiteral("2 wires"))
            && xyInfo->text().contains(QStringLiteral("Grid"));
        auto* to3D = workspace->findChild<QAction*>(QStringLiteral("geometry3DView"));
        auto* toFront = workspace->findChild<QAction*>(QStringLiteral("geometryFrontView"));
        auto* toTop = workspace->findChild<QAction*>(QStringLiteral("geometryTopView"));
        const auto planesClear = clearOver();
        to3D->trigger();
        application.processEvents();
        const auto clear3D = workspace->page() == necwb::ui::GeometryWorkspace::ThreeD
            && workspace->view3D()->isVisible() && clearOver();
        toFront->trigger();
        application.processEvents();
        const auto frontMain = workspace->page() == necwb::ui::GeometryWorkspace::Planes
            && workspace->planes()->mainPlane() == necwb::geometry::ProjectionPlane::XZ
            && toFront->isChecked();
        toTop->trigger();
        application.processEvents();

        // Fit fits every plane on screen, not only the big one.
        auto* fit = workspace->findChild<QAction*>(QStringLiteral("geometryFit"));
        const auto unfit = [&](necwb::ui::GeometryView* view) {
            QWheelEvent wheel(QPointF(50, 50), view->mapToGlobal(QPointF(50, 50)), {}, {0, 240},
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(view, &wheel);
        };
        const auto shownImages = [&] {
            std::vector<QImage> images;
            for (auto* view : workspace->planes()->views()) images.push_back(view->grab().toImage());
            return images;
        };
        const auto fitted = shownImages();
        for (auto* view : workspace->planes()->views()) unfit(view);
        application.processEvents();
        const auto zoomed = shownImages();
        fit->trigger();
        application.processEvents();
        const auto refitted = shownImages();
        auto fitsAll = true;
        for (std::size_t index = 0; index < fitted.size(); ++index)
            fitsAll = fitsAll && zoomed[index] != fitted[index] && refitted[index] == fitted[index];

        // A wire along Y seen from the front is a point: XZ keeps the model's scale and its
        // labels stay distinct and plain.
        necwb::model::AntennaModel alongY;
        alongY.addWire({1, {10.0, -2.0, 0.0}, {10.0, 2.0, 0.0}, 9, 0.001, 1});
        for (auto* view : workspace->views()) view->setModel(alongY);
        application.processEvents();
        const auto distinctLabels = [](const QStringList& labels) {
            auto unique = labels;
            unique.removeDuplicates();
            return labels.size() >= 2 && unique.size() == labels.size()
                && std::ranges::none_of(labels, [](const QString& label) {
                       return label.contains(QLatin1Char('e'));
                   });
        };
        const auto flatLabels = distinctLabels(xz->tickLabels(Qt::Horizontal))
            && distinctLabels(xz->tickLabels(Qt::Vertical))
            && xz->infoText().contains(QStringLiteral("end-on"));
        if (!flatLabels) qWarning() << "flat labels" << xz->tickLabels(Qt::Horizontal) << xz->tickLabels(Qt::Vertical) << xz->infoText();
        for (auto* view : workspace->views()) view->setModel(pair);
        application.processEvents();

        // Currents: the run's values at the column's frequency, as the Currents view shows them.
        necwb::analysis::AnalysisResult run;
        const auto current = [](double frequency, int tag, int segment, double magnitude) {
            return necwb::analysis::SegmentCurrentResult{.frequencyMHz = frequency,
                .segment = segment, .wireTag = tag, .current = {magnitude, 0.0},
                .magnitude = magnitude, .phaseDegrees = 0.0};
        };
        const std::array low{0.25, 0.5, 1.0, 0.75};
        const std::array high{1.0, 0.125, 0.25, 0.5};
        for (auto segment = 1; segment <= 4; ++segment) {
            run.currents.push_back(current(14.0, 1, segment, low[segment - 1]));
            run.currents.push_back(current(14.0, 2, segment, 0.05));
            run.currents.push_back(current(14.2, 1, segment, high[segment - 1]));
            run.currents.push_back(current(14.2, 2, segment, 0.05));
        }
        necwb::ui::ResultsController results(&host);
        results.setColumnChangedCallback([workspace](const necwb::ui::ResultState& state, bool stale) {
            workspace->setResults(state, stale);
        });
        auto* currentsAction = workspace->currentsAction();
        const auto offWithoutResults = !currentsAction->isEnabled() && !xy->showsCurrents();
        const auto data = necwb::ui::ResultData::make(run);
        results.setResults(data, QStringLiteral("currents-run"), pair);
        results.setFrequency(14.0);
        application.processEvents();
        const auto near = [](const QColor& a, const QColor& b) {
            return std::abs(a.red() - b.red()) <= 10 && std::abs(a.green() - b.green()) <= 10
                && std::abs(a.blue() - b.blue()) <= 10;
        };
        // The device pixel holding a point of the view: grab() is at the screen's pixel ratio,
        // and the pixel that contains the point lies wholly inside a stroke centred on it, so it
        // is not an antialiased edge.
        const auto pixelAt = [](const QImage& image, QPointF point) {
            const auto ratio = image.devicePixelRatio();
            return image.pixelColor(QPoint(static_cast<int>(std::floor(point.x() * ratio)),
                static_cast<int>(std::floor(point.y() * ratio))));
        };
        // The colour of each wire 1 segment at its middle, against the Currents view's table.
        const auto matchesCurrentsView = [&](double frequency) {
            necwb::ui::CurrentDistributionView currentsView;
            currentsView.setModel(pair);
            currentsView.setResults(data, QStringLiteral("currents-run"));
            currentsView.setSelectedFrequency(frequency);
            auto* table = currentsView.findChild<QTableWidget*>();
            auto largest = 0.0;
            for (auto row = 0; row < table->rowCount(); ++row)
                largest = std::max(largest, table->item(row, 3)->text().toDouble());
            const auto image = xy->grab().toImage();
            auto matched = 0;
            for (auto row = 0; row < table->rowCount(); ++row) {
                if (table->item(row, 0)->text() != QStringLiteral("1")) continue;
                const auto segment = table->item(row, 1)->text().toInt();
                const auto endpoints = necwb::model::wireSegmentEndpoints(*pair.wireByTag(1), segment);
                const auto middle = xy->screenPoint({(endpoints->first.x + endpoints->second.x) / 2.0,
                    0.0, 0.0});
                const auto expected = necwb::ui::currentColor(table->item(row, 3)->text().toDouble(), largest);
                if (near(pixelAt(image, middle), expected)) ++matched;
                else qWarning() << "segment" << segment << pixelAt(image, middle) << expected;
            }
            return matched == 4;
        };
        const auto colouredLow = currentsAction->isEnabled() && currentsAction->isChecked()
            && xy->showsCurrents() && matchesCurrentsView(14.0)
            && workspace->planes()->panes()[0]->colorScale()->isVisible()
            && workspace->planes()->panes()[0]->colorScale()->maximumText() == QStringLiteral("1.000 A")
            && xyInfo->text().contains(QStringLiteral("14.000 MHz"));
        const auto firstSegment = xy->screenPoint({-1.5, 0.0, 0.0});
        const auto lowColour = pixelAt(xy->grab().toImage(), firstSegment);
        results.setFrequency(14.2);
        application.processEvents();
        const auto followsFrequency = matchesCurrentsView(14.2)
            && !near(pixelAt(xy->grab().toImage(), firstSegment), lowColour);

        // The selected wire keeps an outline that does not rely on hue.
        const auto beside = xy->screenPoint({-1.5, 0.0, 0.0}) + QPointF(0.0, 4.0);
        const auto unselected = xy->grab().toImage();
        for (auto* view : workspace->views()) view->selectWire(1);
        application.processEvents();
        const auto selectedImage = xy->grab().toImage();
        const auto selectionShows = selectedImage != unselected
            && near(pixelAt(selectedImage, beside), xy->palette().color(QPalette::Text))
            && !near(pixelAt(unselected, beside), xy->palette().color(QPalette::Text));
        for (auto* view : workspace->views()) view->selectWire(-1);

        // Stale results: plain wires, and the info line says why.
        results.setStale(true);
        application.processEvents();
        const auto stalePlain = !xy->showsCurrents() && !workspace->view3D()->showsCurrents()
            && near(pixelAt(xy->grab().toImage(), firstSegment), QColor(40, 105, 170))
            && xyInfo->text().contains(QStringLiteral("out of date"))
            && !workspace->planes()->panes()[0]->colorScale()->isVisible();
        // A live solve brings currents back at its frequency.
        necwb::analysis::AnalysisResult livePoint;
        for (auto segment = 1; segment <= 4; ++segment)
            livePoint.currents.push_back(current(14.1, 1, segment, 0.5));
        results.setLivePoint(livePoint, 14.1);
        results.setFrequency(14.1);
        application.processEvents();
        const auto liveShown = xy->showsCurrents() && xyInfo->text().contains(QStringLiteral("14.100"));
        // Turning Currents off is remembered.
        currentsAction->setChecked(false);
        application.processEvents();
        const auto offRemembered = !xy->showsCurrents()
            && !QSettings{}.value(QStringLiteral("geometry/showCurrents")).toBool();
        currentsAction->setChecked(true);
        if (!infoUnder || !planesClear || !clear3D || !frontMain || !fitsAll || !flatLabels
            || !offWithoutResults || !colouredLow || !followsFrequency || !selectionShows
            || !stalePlain || !liveShown || !offRemembered) {
            qWarning() << "geometry extras" << infoUnder << planesClear << clear3D << frontMain
                       << fitsAll << flatLabels << offWithoutResults << colouredLow
                       << followsFrequency << selectionShows << stalePlain << liveShown
                       << offRemembered << xyInfo->text();
            return NECWB_SMOKE_FAILURE("geometry info lines, view buttons, labels and currents");
        }
    }
    // The line under a card table sits under its last row, or at the bottom, visible, when
    // the table is taller than the pane.
    NECWB_SMOKE_STEP();
    {
        necwb::ui::WireCardEditor wires;
        wires.resize(600, 420);
        necwb::model::AntennaModel few;
        for (auto tag = 1; tag <= 3; ++tag)
            few.addWire({tag, {0.0, 0.0, tag * 1.0}, {4.0, 0.0, tag * 1.0}, 5, 0.001, static_cast<std::size_t>(tag)});
        wires.setModel(few);
        wires.show();
        application.processEvents();
        application.processEvents();
        auto* table = wires.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
        auto* note = wires.findChild<QLabel*>(QStringLiteral("cardRowNote"));
        wires.selectWire(1);
        application.processEvents();
        const auto lastRow = table->mapTo(&wires, table->visualItemRect(table->item(2, 0)).bottomLeft()).y()
            + table->viewport()->y();
        const auto shortUnder = note->isVisible() && note->y() >= table->geometry().bottom()
            && note->y() - lastRow <= 12 && note->text().contains(QStringLiteral("long"));
        necwb::model::AntennaModel many;
        for (auto tag = 1; tag <= 60; ++tag)
            many.addWire({tag, {0.0, 0.0, tag * 1.0}, {4.0, 0.0, tag * 1.0}, 5, 0.001, static_cast<std::size_t>(tag)});
        wires.setModel(many);
        application.processEvents();
        application.processEvents();
        const auto tallVisible = note->isVisible() && note->geometry().bottom() <= wires.height()
            && table->geometry().bottom() < note->y() && table->verticalScrollBar()->isVisible();
        if (!shortUnder || !tallVisible) {
            qWarning() << "row note" << shortUnder << tallVisible << note->geometry() << table->geometry() << lastRow;
            return NECWB_SMOKE_FAILURE("the line under a table follows its last row");
        }
    }
    // Opening an existing model shows it in the main window without raising the NEC Deck
    // window, the left column stops at its maximum width, and at its minimum size the window
    // still has room to open the bottom panel.
    auto openModelKeepsDeckClosed = false;
    auto leftColumnCappedAtContent = false;
    auto bottomPanelOpensAtMinimumSize = false;
    NECWB_SMOKE_STEP();
    {
        const auto savedRecentModels = resultWindowSettings.value(
            QStringLiteral("files/recentModels"));
        QTemporaryDir modelDirectory;
        const auto modelPath = modelDirectory.filePath(QStringLiteral("open-smoke.nec"));
        QFile modelFile(modelPath);
        if (modelDirectory.isValid() && modelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            modelFile.write("GW 1 11 -5 0 6 5 0 6 0.001\nGE 0\nEN\n");
            modelFile.close();
            resultWindowSettings.setValue(QStringLiteral("files/recentModels"),
                QStringList{modelPath});
            resultWindowSettings.sync();
            necwb::ui::MainWindow mainWindow;
            mainWindow.resize(1400, 900);
            mainWindow.show();
            application.processEvents();
            auto* recentModelsMenu = mainWindow.findChild<QMenu*>(
                QStringLiteral("recentModelsMenu"));
            QAction* openRecent = nullptr;
            for (auto* action : recentModelsMenu != nullptr ? recentModelsMenu->actions()
                                                            : QList<QAction*>{}) {
                if (action->data().toString() == modelPath) openRecent = action;
            }
            auto* split = mainWindow.findChild<QSplitter*>(QStringLiteral("workbenchSplit"));
            auto* leftColumn = mainWindow.findChild<QWidget*>(QStringLiteral("modelColumn"));
            if (openRecent != nullptr && split != nullptr && leftColumn != nullptr) {
                openRecent->trigger();
                application.processEvents();
                const auto* modelEditorWindow = topLevelDialog(
                    QStringLiteral("modelEditorWindow"));
                openModelKeepsDeckClosed = split->isVisible()
                    && (modelEditorWindow == nullptr || !modelEditorWindow->isVisible());
                // Drag the split handle far to the right.
                auto* handle = split->handle(1);
                const QPointF grab(handle->width() / 2.0, handle->height() / 2.0);
                const QPointF dragTo(grab.x() + 800, grab.y());
                QMouseEvent press(QEvent::MouseButtonPress, grab, handle->mapToGlobal(grab),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent move(QEvent::MouseMove, dragTo, handle->mapToGlobal(dragTo),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, dragTo,
                    handle->mapToGlobal(dragTo), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(handle, &press);
                QApplication::sendEvent(handle, &move);
                QApplication::sendEvent(handle, &release);
                application.processEvents();
                leftColumnCappedAtContent = leftColumn->maximumWidth() < QWIDGETSIZE_MAX
                    && split->sizes().value(0) == leftColumn->maximumWidth()
                    && leftColumn->width() == leftColumn->maximumWidth();
                auto* toggleBottomPanel = mainWindow.findChild<QAction*>(
                    QStringLiteral("toggleBottomPanelAction"));
                if (toggleBottomPanel != nullptr && !toggleBottomPanel->isChecked()) {
                    mainWindow.resize(mainWindow.minimumSizeHint());
                    application.processEvents();
                    const auto minimumSize = mainWindow.size();
                    toggleBottomPanel->trigger();
                    application.processEvents();
                    bottomPanelOpensAtMinimumSize = toggleBottomPanel->isChecked()
                        && mainWindow.size() == minimumSize;
                }
            }
            mainWindow.close();
        }
        resultWindowSettings.setValue(QStringLiteral("files/recentModels"), savedRecentModels);
        resultWindowSettings.remove(QStringLiteral("mainWindow/v2"));
        resultWindowSettings.remove(QStringLiteral("ui/bottomPanel"));
        resultWindowSettings.sync();
    }
    // Editing a cell rewrites only its own field: SY symbols and exact numbers in the other
    // fields stay in the deck as written, in the Wires table and the card tables alike.
    auto wirePaneKeepsSymbols = false;
    NECWB_SMOKE_STEP();
    {
        const auto savedRecentModels = resultWindowSettings.value(
            QStringLiteral("files/recentModels"));
        QTemporaryDir modelDirectory;
        const auto modelPath = modelDirectory.filePath(QStringLiteral("symbolic-wires.nec"));
        QFile modelFile(modelPath);
        if (modelDirectory.isValid() && modelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            modelFile.write("SY half=0.485, x2=0.5\nGW 1 11 0 -half 0 0 half 0 0.00123456\n"
                "GW 2 11 x2 half 0 x2 -half 0 0.003\nGA 3 8 0.5 0 90 0.001\nGE 0\n"
                "LD 0 1 3 3 12.3456789 0 0\nEN\n");
            modelFile.close();
            resultWindowSettings.setValue(QStringLiteral("files/recentModels"),
                QStringList{modelPath});
            resultWindowSettings.sync();
            necwb::ui::MainWindow mainWindow;
            mainWindow.resize(1400, 900);
            mainWindow.show();
            application.processEvents();
            if (auto* recentModelsMenu = mainWindow.findChild<QMenu*>(
                    QStringLiteral("recentModelsMenu"))) {
                for (auto* action : recentModelsMenu->actions()) {
                    if (action->data().toString() != modelPath) continue;
                    action->trigger();
                    break;
                }
            }
            application.processEvents();
            auto* tree = mainWindow.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
            auto* deck = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor"));
            auto* wireCells = mainWindow.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
            auto* loadCells = mainWindow.findChild<QWidget*>(QStringLiteral("cardsLD"))
                ->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
            const auto pick = [&](const QString& name) {
                auto* item = tree->findItems(name, Qt::MatchExactly | Qt::MatchRecursive).value(0);
                if (item != nullptr) tree->setCurrentItem(item);
                application.processEvents();
                return item != nullptr;
            };
            const auto setCell = [&](QTableWidget* table, int column, const QString& value) {
                table->item(table->currentRow(), column)->setText(value);
                application.processEvents();
            };
            if (deck != nullptr && wireCells != nullptr && loadCells != nullptr
                && pick(QStringLiteral("Wire 2"))) {
                // End Z of wire 2, whose Y is -half; then end Z of wire 1 and L of the load.
                const auto endYLinked = wireCells->currentRow() == 1
                    && wireCells->item(1, 6)->data(necwb::ui::CellExpressionRole).toString() == QStringLiteral("-half");
                setCell(wireCells, 7, QStringLiteral("0.01"));
                const auto symbolsKept = deck->toPlainText().contains(
                    QStringLiteral("GW 2 11 x2 half 0 x2 -half 0.01 0.003"));
                const auto wireExact = pick(QStringLiteral("Wire 1")) && wireCells->currentRow() == 0
                    && (setCell(wireCells, 7, QStringLiteral("0.02")), true)
                    && deck->toPlainText().contains(QStringLiteral("GW 1 11 0 -half 0 0 half 0.02 0.00123456"));
                const auto loadExact = pick(QStringLiteral("W1 S3")) && loadCells->currentRow() == 0
                    && (setCell(loadCells, 7, QStringLiteral("1e-6")), true)
                    && deck->toPlainText().contains(QStringLiteral("LD 0 1 3 3 12.3456789 1e-6 0"))
                    && loadCells->currentRow() == 0 && !loadCells->selectedItems().isEmpty();
                // An arc is its GA row in the Other table.
                auto* otherCells = mainWindow.findChild<QWidget*>(QStringLiteral("cardsOther"))
                    ->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
                const auto* paneTitle = mainWindow.findChild<QLabel*>(QStringLiteral("propertiesPaneTitle"));
                const auto arcRow = pick(QStringLiteral("Wire 3")) && otherCells->isVisible()
                    && paneTitle->text() == QStringLiteral("Other cards") && otherCells->currentRow() >= 0
                    && otherCells->item(otherCells->currentRow(), 1)->text() == QStringLiteral("GA");
                wirePaneKeepsSymbols = endYLinked && symbolsKept && wireExact && loadExact && arcRow;
                if (!wirePaneKeepsSymbols)
                    qWarning() << "exact cells" << endYLinked << symbolsKept << wireExact << loadExact << arcRow
                               << deck->toPlainText();
            }
            // The edit is not saved; closing must not stop to ask.
            if (deck != nullptr) deck->document()->setModified(false);
            mainWindow.close();
        }
        resultWindowSettings.setValue(QStringLiteral("files/recentModels"), savedRecentModels);
        resultWindowSettings.remove(QStringLiteral("mainWindow/v2"));
        resultWindowSettings.sync();
    }
    // A wire GX made is read-only: no ƒx from the GX card's fields, and its tooltip names GX,
    // not a GC taper. Wire 1's own expression still shows.
    NECWB_SMOKE_STEP();
    {
        const auto savedRecentModels = resultWindowSettings.value(
            QStringLiteral("files/recentModels"));
        QTemporaryDir modelDirectory;
        const auto modelPath = modelDirectory.filePath(QStringLiteral("mirrored-dipole.nec"));
        QFile modelFile(modelPath);
        auto generatedWirePlain = false;
        if (modelDirectory.isValid() && modelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            modelFile.write("SY half=16.5\nGW 1 11 -half 0 30 0 0 30 0.0033\nGX 1 100\n"
                "GS 0 0 0.3048\nGE 0\nEX 0 1 11 0 1 0\nFR 0 1 0 0 14.175 0\nEN\n");
            modelFile.close();
            resultWindowSettings.setValue(QStringLiteral("files/recentModels"),
                QStringList{modelPath});
            resultWindowSettings.sync();
            necwb::ui::MainWindow mainWindow;
            mainWindow.resize(1400, 900);
            mainWindow.show();
            application.processEvents();
            if (auto* recentModelsMenu = mainWindow.findChild<QMenu*>(
                    QStringLiteral("recentModelsMenu"))) {
                for (auto* action : recentModelsMenu->actions()) {
                    if (action->data().toString() != modelPath) continue;
                    action->trigger();
                    break;
                }
            }
            application.processEvents();
            auto* tree = mainWindow.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
            auto* wireCells = mainWindow.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
            auto* item = tree->findItems(QStringLiteral("Wire 2"), Qt::MatchExactly | Qt::MatchRecursive).value(0);
            if (item != nullptr) tree->setCurrentItem(item);
            application.processEvents();
            QStringList marked, tips;
            if (wireCells != nullptr && wireCells->rowCount() == 2) {
                for (auto column = 0; column <= 8; ++column) {
                    auto* cell = wireCells->item(1, column);
                    if (cell == nullptr) continue;
                    // What shows ƒx: the expression, its icon and italics.
                    if (!cell->data(necwb::ui::CellExpressionRole).toString().isEmpty()
                        || !cell->icon().isNull() || cell->font().italic())
                        marked << QString::number(column);
                    tips << cell->toolTip();
                }
                generatedWirePlain = item != nullptr && marked.isEmpty() && tips.size() == 9
                    && std::ranges::all_of(tips, [](const QString& tip) {
                           return tip.contains(QStringLiteral("GX card on line 3")) && !tip.contains(QStringLiteral("GC"));
                       })
                    && !(wireCells->item(1, 2)->flags() & Qt::ItemIsEditable)
                    && wireCells->item(0, 2)->data(necwb::ui::CellExpressionRole).toString() == QStringLiteral("-half")
                    && !wireCells->item(0, 2)->icon().isNull();
            }
            // Other opens on the family that holds the GX card, with its fields named and
            // the line under the table saying what it makes.
            auto gxShown = false;
            QStringList gxSeen;
            if (auto* other = tree->findItems(QStringLiteral("Other"), Qt::MatchExactly | Qt::MatchRecursive).value(0)) {
                tree->setCurrentItem(other);
                emit tree->itemClicked(other, 0);
                application.processEvents();
                auto* otherPage = mainWindow.findChild<QWidget*>(QStringLiteral("cardsOther"));
                auto* otherCells = otherPage->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
                auto* families = otherPage->findChild<QTreeWidget*>();
                auto gxRow = -1;
                for (auto row = 0; row < otherCells->rowCount(); ++row)
                    if (otherCells->item(row, 1)->text() == QStringLiteral("GX")) gxRow = row;
                if (gxRow >= 0) otherCells->selectRow(gxRow);
                application.processEvents();
                const auto* note = otherPage->findChild<QLabel*>(QStringLiteral("cardRowNote"));
                const auto header = [otherCells](int column) {
                    auto* item = otherCells->horizontalHeaderItem(column);
                    return item != nullptr ? item->text() : QString{};
                };
                const auto noteText = note != nullptr ? note->toolTip() : QString{};
                gxSeen << (families->currentItem() ? families->currentItem()->text(0) : QString{})
                       << QString::number(gxRow) << header(2) << header(3) << noteText;
                gxShown = otherCells->isVisible() && gxRow >= 0
                    && families->currentItem() != nullptr
                    && families->currentItem()->text(0).startsWith(QStringLiteral("Other NEC-2 Geometry Cards"))
                    && header(2) == QStringLiteral("Tag increment")
                    && header(3) == QStringLiteral("Mirror in YZ XZ XY")
                    && noteText.contains(QStringLiteral("in the YZ plane"))
                    && noteText.contains(QStringLiteral("makes wire 2"));
            }
            if (!gxShown) qWarning() << "GX in Other" << gxSeen;
            generatedWirePlain = generatedWirePlain && gxShown;
            if (!generatedWirePlain)
                qWarning() << "generated wire" << (wireCells ? wireCells->rowCount() : -1) << marked << tips.value(2);
            mainWindow.close();
        }
        resultWindowSettings.setValue(QStringLiteral("files/recentModels"), savedRecentModels);
        resultWindowSettings.remove(QStringLiteral("mainWindow/v2"));
        resultWindowSettings.sync();
        if (!generatedWirePlain)
            return NECWB_SMOKE_FAILURE("a GX wire shows no expressions and names its card");
    }
    // Dragging a variable's slider rewrites its SY card once, on release, as one undo entry;
    // selecting Variables shows the Parameters table in the properties pane.
    auto variableSliderEdits = false;
    NECWB_SMOKE_STEP();
    {
        const auto savedRecentModels = resultWindowSettings.value(
            QStringLiteral("files/recentModels"));
        QTemporaryDir modelDirectory;
        const auto modelPath = modelDirectory.filePath(QStringLiteral("variable-slider.nec"));
        QFile modelFile(modelPath);
        if (modelDirectory.isValid() && modelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            modelFile.write("SY len=0.5\nGW 1 11 0 -len 0 0 len 0 0.001\nGE 0\nEN\n");
            modelFile.close();
            resultWindowSettings.setValue(QStringLiteral("files/recentModels"),
                QStringList{modelPath});
            resultWindowSettings.sync();
            necwb::ui::MainWindow mainWindow;
            mainWindow.resize(1400, 900);
            mainWindow.show();
            application.processEvents();
            if (auto* recentModelsMenu = mainWindow.findChild<QMenu*>(
                    QStringLiteral("recentModelsMenu"))) {
                for (auto* action : recentModelsMenu->actions()) {
                    if (action->data().toString() != modelPath) continue;
                    action->trigger();
                    break;
                }
            }
            application.processEvents();
            auto* deck = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor"));
            auto* slider = mainWindow.findChild<QSlider*>(QStringLiteral("variableSlider"));
            QAction* undo = nullptr;
            for (auto* action : mainWindow.findChildren<QAction*>())
                if (action->text().startsWith(QStringLiteral("&Undo"))) undo = action;
            if (deck != nullptr && slider != nullptr && undo != nullptr) {
                const auto undoBefore = undo->text();
                slider->setSliderDown(true);
                slider->setValue(900);
                slider->setValue(1000);
                application.processEvents();
                const auto unchangedDuringDrag = deck->toPlainText().startsWith(
                    QStringLiteral("SY len=0.5\n"));
                slider->setSliderDown(false);
                application.processEvents();
                const auto changedOnRelease = deck->toPlainText().startsWith(
                    QStringLiteral("SY len=0.625\n"));
                const auto oneUndoEntry = undo->isEnabled()
                    && undo->text() == QStringLiteral("&Undo Edit parameter len");
                undo->trigger();
                application.processEvents();
                const auto undoRestores = deck->toPlainText().startsWith(
                    QStringLiteral("SY len=0.5\n")) && undo->text() == undoBefore;
                auto* tree = mainWindow.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
                for (auto index = 0; tree != nullptr && index < tree->topLevelItemCount(); ++index) {
                    if (tree->topLevelItem(index)->text(0) == QStringLiteral("Variables"))
                        tree->setCurrentItem(tree->topLevelItem(index));
                }
                application.processEvents();
                const auto* parameterTable = mainWindow.findChild<QWidget*>(
                    QStringLiteral("parameterTable"));
                const auto* pane = mainWindow.findChild<QWidget*>(QStringLiteral("propertiesPane"));
                const auto parametersInPane = parameterTable != nullptr && pane != nullptr
                    && pane->isAncestorOf(parameterTable) && parameterTable->isVisible();
                variableSliderEdits = unchangedDuringDrag && changedOnRelease && oneUndoEntry
                    && undoRestores && parametersInPane;
                if (!variableSliderEdits)
                    qWarning() << "variable slider" << unchangedDuringDrag << changedOnRelease
                               << oneUndoEntry << undo->text() << undoRestores << parametersInPane;
            }
            if (deck != nullptr) deck->document()->setModified(false);
            mainWindow.close();
        }
        resultWindowSettings.setValue(QStringLiteral("files/recentModels"), savedRecentModels);
        resultWindowSettings.remove(QStringLiteral("mainWindow/v2"));
        resultWindowSettings.sync();
    }
    // A source placed along its wire: typed as 50% in the Sources table, kept in the EX card's
    // 4nec2 comment, moved by a segment-count variable, undone and redone, saved and reopened,
    // and put back on an exact segment; the optimizer will only sweep the count in whole steps.
    auto sourcePlacement = false;
    NECWB_SMOKE_STEP();
    {
        QSettings settings;
        const auto savedRecent = settings.value(QStringLiteral("files/recentModels"));
        const auto savedLive = settings.value(QStringLiteral("variables/live"));
        settings.setValue(QStringLiteral("variables/live"), false);
        QTemporaryDir directory;
        const auto modelPath = directory.filePath(QStringLiteral("placed-source.nec"));
        QFile modelFile(modelPath);
        if (directory.isValid() && modelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            modelFile.write("SY segments=11, len=5\nGW 1 segments 0 -len 0 0 len 0 .001\nGE 0\n"
                            "EX 0 1 3 0 1 0\nFR 0 1 0 0 14 0\nEN\n");
            modelFile.close();
        }
        settings.setValue(QStringLiteral("files/recentModels"), QStringList{modelPath});
        settings.sync();
        const auto openModel = [&](necwb::ui::MainWindow& window) {
            window.resize(1400, 900);
            window.show();
            application.processEvents();
            // Opening rebuilds the menu, so nothing after the trigger reads it.
            if (auto* recent = window.findChild<QMenu*>(QStringLiteral("recentModelsMenu"))) {
                for (auto* action : recent->actions()) {
                    if (action->data().toString() != modelPath) continue;
                    action->trigger();
                    break;
                }
            }
            application.processEvents();
        };
        const auto waitFor = [&](const auto& done) {
            QElapsedTimer waited;
            waited.start();
            while (!done() && waited.elapsed() < 3000) application.processEvents(QEventLoop::AllEvents, 20);
            return done();
        };
        const auto action = [](necwb::ui::MainWindow& window, const QString& prefix) -> QAction* {
            for (auto* found : window.findChildren<QAction*>())
                if (found->text().startsWith(prefix)) return found;
            return nullptr;
        };
        necwb::ui::MainWindow mainWindow;
        openModel(mainWindow);
        auto* deck = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor"));
        auto* sources = mainWindow.findChild<QWidget*>(QStringLiteral("cardsEX"));
        auto* table = sources == nullptr ? nullptr
            : sources->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
        auto* note = sources == nullptr ? nullptr
            : sources->findChild<QLabel*>(QStringLiteral("cardRowNote"));
        auto* undo = action(mainWindow, QStringLiteral("&Undo"));
        auto* redo = action(mainWindow, QStringLiteral("&Redo"));
        auto* save = action(mainWindow, QStringLiteral("&Save"));
        auto* segmentsValue = mainWindow.findChild<QDoubleSpinBox*>(QStringLiteral("variableValue"));
        if (deck != nullptr && table != nullptr && note != nullptr && undo != nullptr && redo != nullptr
            && save != nullptr && segmentsValue != nullptr && table->rowCount() == 1) {
            const auto text = [deck] { return deck->toPlainText(); };
            table->selectRow(0);
            const auto exactNote = note->text().contains(
                QStringLiteral("Segment 3 of 11, its middle 22.73% from End 1"));
            // Right-click on the segment offers the placements.
            QStringList offered;
            QTimer::singleShot(100, [&offered] {
                if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                    for (const auto* item : menu->actions())
                        if (!item->isSeparator()) offered << item->text();
                    menu->close();
                }
            });
            emit table->customContextMenuRequested(table->visualItemRect(table->item(0, 4)).center());
            application.processEvents();
            const auto menuOffers = offered == QStringList{QStringLiteral("Make Variable…"),
                QStringLiteral("Center of Wire (50%)"), QStringLiteral("Along Wire…")};
            table->item(0, 4)->setText(QStringLiteral("50%"));
            const auto centered = waitFor([&] {
                    return text().contains(QStringLiteral("EX 0 1 6 0 1 0 ' necwb:at=50%\n")); })
                && text().startsWith(QStringLiteral("SY segments=11, len=5\nGW 1 segments 0 -len 0 0 len 0 .001\n"))
                && waitFor([&] { return table->item(0, 4)->text() == QStringLiteral("6"); })
                && table->item(0, 4)->data(necwb::ui::CellExpressionRole).toString() == QStringLiteral("50%");
            table->selectRow(0);
            const auto centeredNote = note->text().contains(
                QStringLiteral("50% along wire 1 → segment 6 of 11, its middle 50% from End 1"));
            // Ten segments: the source moves, and the line says no segment is centered.
            segmentsValue->setValue(10.0);
            emit segmentsValue->editingFinished();
            const auto followed = waitFor([&] {
                    return text().contains(QStringLiteral("SY segments=10, len=5\n"))
                        && text().contains(QStringLiteral("EX 0 1 5 0 1 0 ' necwb:at=50%\n")); });
            table->selectRow(0);
            const auto evenNote = note->text().contains(QStringLiteral("segment 5 of 10, its middle 45% "
                "from End 1 · with an even number of equal segments no segment is centered"));
            undo->trigger();
            application.processEvents();
            const auto undone = text().contains(QStringLiteral("SY segments=11, len=5\n"))
                && text().contains(QStringLiteral("EX 0 1 6 0 1 0 ' necwb:at=50%\n"));
            redo->trigger();
            application.processEvents();
            const auto redone = text().contains(QStringLiteral("EX 0 1 5 0 1 0 ' necwb:at=50%\n"));
            save->trigger();
            application.processEvents();
            QFile saved(modelPath);
            const auto savedText = saved.open(QIODevice::ReadOnly | QIODevice::Text)
                ? QString::fromUtf8(saved.readAll()) : QString{};
            saved.close();
            // Reopened, it is still placed along the wire, not fixed on segment 5.
            auto reopened = false;
            {
                necwb::ui::MainWindow again;
                openModel(again);
                auto* againSources = again.findChild<QWidget*>(QStringLiteral("cardsEX"));
                auto* againTable = againSources == nullptr ? nullptr
                    : againSources->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
                reopened = againTable != nullptr && againTable->rowCount() == 1
                    && againTable->item(0, 4)->text() == QStringLiteral("5")
                    && againTable->item(0, 4)->data(necwb::ui::CellExpressionRole).toString()
                        == QStringLiteral("50%");
            }
            // A segment number fixes it there again, without the mark.
            table->item(0, 4)->setText(QStringLiteral("4"));
            const auto fixed = waitFor([&] { return text().contains(QStringLiteral("EX 0 1 4 0 1 0\nFR")); })
                && !text().contains(QStringLiteral("necwb"));
            // The optimizer leaves segments unchecked and sweeps it only in whole steps.
            auto* variables = optimizerChild<QTableWidget>(QStringLiteral("optimizationVariablesTable"));
            auto* method = optimizerChild<QComboBox>(QStringLiteral("optimizationSearchMethod"));
            auto* points = optimizerChild<QSpinBox>(QStringLiteral("optimizationCandidateCount"));
            auto* run = optimizerChild<QPushButton>(QStringLiteral("optimizationRun"));
            auto optimizerGuarded = false;
            if (variables != nullptr && method != nullptr && points != nullptr && run != nullptr
                && variables->rowCount() == 2) {
                const auto segmentsUnchecked = variables->item(0, 1)->text() == QStringLiteral("segments")
                    && variables->item(0, 0)->checkState() == Qt::Unchecked
                    && variables->item(1, 0)->checkState() == Qt::Checked
                    && variables->item(0, 0)->toolTip().contains(QStringLiteral("GW field 2 on line 2"));
                variables->item(1, 0)->setCheckState(Qt::Unchecked);
                variables->item(0, 0)->setCheckState(Qt::Checked);
                method->setCurrentIndex(method->findText(QStringLiteral("Adaptive Optimize")));
                application.processEvents();
                const auto searchRefused = !run->isEnabled() && run->toolTip().startsWith(
                    QStringLiteral("segments sets a whole number (GW field 2 on line 2), and Adaptive"));
                method->setCurrentIndex(method->findText(QStringLiteral("Parameter Sweep")));
                variables->item(0, 3)->setText(QStringLiteral("5"));
                variables->item(0, 4)->setText(QStringLiteral("21"));
                points->setValue(8);
                application.processEvents();
                const auto halfStepsRefused = run->toolTip().startsWith(
                    QStringLiteral("At segments = 7.28571 the sweep makes GW field 2 on line 2 fractional"));
                points->setValue(9);
                application.processEvents();
                const auto wholeStepsAllowed = !run->toolTip().contains(QStringLiteral("whole"))
                    && !run->toolTip().contains(QStringLiteral("fractional"));
                optimizerGuarded = segmentsUnchecked && searchRefused && halfStepsRefused && wholeStepsAllowed;
                if (!optimizerGuarded)
                    qWarning() << "optimizer guard" << segmentsUnchecked << searchRefused << halfStepsRefused
                               << wholeStepsAllowed << run->toolTip();
            }
            sourcePlacement = exactNote && menuOffers && centered && centeredNote && followed && evenNote
                && undone && redone && savedText.contains(QStringLiteral("EX 0 1 5 0 1 0 ' necwb:at=50%\n"))
                && reopened && fixed && optimizerGuarded;
            if (!sourcePlacement)
                qWarning() << "source placement" << exactNote << menuOffers << offered << centered
                           << centeredNote << followed << evenNote << undone << redone << reopened << fixed
                           << note->text() << text();
            deck->document()->setModified(false);
        }
        settings.setValue(QStringLiteral("files/recentModels"), savedRecent);
        if (savedLive.isValid()) settings.setValue(QStringLiteral("variables/live"), savedLive);
        else settings.remove(QStringLiteral("variables/live"));
        settings.sync();
    }
    // Automatic Segmentation in the window: a wire with a symbolic count is skipped and says so,
    // a 25% source goes to the segment its mark gives, Apply writes the deck the preview showed,
    // and one Undo restores the deck.
    auto segmentationPreviewApplied = false;
    NECWB_SMOKE_STEP();
    {
        QSettings settings;
        const auto savedRecent = settings.value(QStringLiteral("files/recentModels"));
        QTemporaryDir directory;
        const auto modelPath = directory.filePath(QStringLiteral("auto-segmentation.nec"));
        const QByteArray original = "SY nseg=11\nGW 1 11 0 -5 0 0 5 0 .001\nGW 2 nseg 0 -5 1 0 5 1 .001\n"
                                    "GE 0\nEX 0 1 3 0 1 0 ' necwb:at=25%\nFR 0 1 0 0 30 0\nEN\n";
        QFile modelFile(modelPath);
        if (directory.isValid() && modelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            modelFile.write(original);
            modelFile.close();
        }
        settings.setValue(QStringLiteral("files/recentModels"), QStringList{modelPath});
        settings.sync();
        necwb::ui::MainWindow mainWindow;
        mainWindow.resize(1400, 900);
        mainWindow.show();
        application.processEvents();
        if (auto* recent = mainWindow.findChild<QMenu*>(QStringLiteral("recentModelsMenu"))) {
            for (auto* action : recent->actions()) {
                if (action->data().toString() != modelPath) continue;
                action->trigger();
                break;
            }
        }
        application.processEvents();
        auto* deck = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor"));
        auto* undo = mainWindow.findChild<QAction*>(QStringLiteral("undoAction"));
        QAction* segmentation = nullptr;
        for (auto* found : mainWindow.findChildren<QAction*>())
            if (found->text() == QString::fromUtf8("Automatic Segmentation…")) segmentation = found;
        QString previewedSource;
        QString skippedRow;
        QString sourceRow;
        QString summary;
        if (deck != nullptr && undo != nullptr && segmentation != nullptr
            && deck->toPlainText().toUtf8() == original) {
            QTimer::singleShot(100, [&] {
                auto* dialog = dynamic_cast<necwb::ui::AutoSegmentationDialog*>(
                    QApplication::activeModalWidget());
                if (dialog == nullptr) return;
                auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("segmentationTable"));
                auto* label = dialog->findChild<QLabel*>(QStringLiteral("segmentationSummary"));
                if (table != nullptr && table->rowCount() == 2) {
                    sourceRow = table->item(0, 3)->text() + QLatin1Char('|') + table->item(0, 5)->text();
                    skippedRow = table->item(1, 3)->text() + QLatin1Char('|') + table->item(1, 5)->text();
                }
                if (label != nullptr) summary = label->text();
                previewedSource = QString::fromStdString(dialog->edit().source);
                if (auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("applySegmentationButton")))
                    apply->click();
                else dialog->reject();
            });
            segmentation->trigger();
            application.processEvents();
            const auto applied = deck->toPlainText();
            const auto previewMatchesApply = !previewedSource.isEmpty() && applied == previewedSource
                && applied.contains(QStringLiteral("GW 1 21 0 -5 0 0 5 0 .001\n"))
                && applied.contains(QStringLiteral("GW 2 nseg 0 -5 1 0 5 1 .001\n"))
                && applied.contains(QStringLiteral("SY nseg=11\n"))
                && applied.contains(QStringLiteral("EX 0 1 6 0 1 0 ' necwb:at=25%\n"));
            const auto previewShown = sourceRow == QString::fromUtf8("21|EX at 25%: 3 → 6")
                && skippedRow == QString::fromUtf8("21 (skipped)|Skipped: its segment count is nseg on line 3")
                && summary.startsWith(QStringLiteral("1 of 2 wire(s) will change."))
                && summary.contains(QStringLiteral("1 wire(s) are skipped"));
            undo->trigger();
            application.processEvents();
            const auto undone = deck->toPlainText().toUtf8() == original;
            segmentationPreviewApplied = previewMatchesApply && previewShown && undone;
            if (!segmentationPreviewApplied)
                qWarning() << "automatic segmentation" << previewMatchesApply << previewShown << undone
                           << sourceRow << skippedRow << summary << applied;
        } else {
            qWarning() << "automatic segmentation setup" << (deck != nullptr) << (undo != nullptr)
                       << (segmentation != nullptr) << (deck ? deck->toPlainText() : QString{});
        }
        if (deck != nullptr) deck->document()->setModified(false);
        settings.setValue(QStringLiteral("files/recentModels"), savedRecent);
        settings.sync();
    }
    // Variables live: the wires follow a drag with the deck untouched, a release is one undo
    // step, Ctrl+Z works from a value box and the tree, Reset and Revert are one step each,
    // and a live solve uses a one-frequency copy of the deck without adding to Runs history.
    auto variablesLive = false;
    NECWB_SMOKE_STEP();
    {
        QSettings settings;
        const auto saved = std::array{std::pair{QStringLiteral("files/recentModels"),
            settings.value(QStringLiteral("files/recentModels"))},
            std::pair{QStringLiteral("analysis/executables/nec2"),
                settings.value(QStringLiteral("analysis/executables/nec2"))},
            std::pair{QStringLiteral("analysis/backendId"),
                settings.value(QStringLiteral("analysis/backendId"))},
            std::pair{QStringLiteral("variables/live"), settings.value(QStringLiteral("variables/live"))}};
        QTemporaryDir directory;
        const auto modelPath = directory.filePath(QStringLiteral("live.nec"));
        const auto inputsPath = directory.filePath(QStringLiteral("inputs"));
        QDir().mkpath(inputsPath);
        const QByteArray modelText = "SY len=0.5\nGW 1 11 0 -len 0 0 len 0 0.001\nGE 0\n"
            "EX 0 1 6 0 1 0\nFR 0 5 0 0 144 1\nRP 0 1 1 1000 90 0 0 0\nEN\n";
        QFile modelFile(modelPath);
        if (directory.isValid() && modelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            modelFile.write(modelText);
            modelFile.close();
            // This binary is the solver: it keeps each input and answers at its frequency.
            qputenv("NECWB_FAKE_SOLVER_INPUTS", inputsPath.toLocal8Bit());
            settings.setValue(QStringLiteral("files/recentModels"), QStringList{modelPath});
            settings.setValue(QStringLiteral("analysis/executables/nec2"),
                QCoreApplication::applicationFilePath());
            settings.setValue(QStringLiteral("analysis/backendId"), QStringLiteral("nec2"));
            settings.setValue(QStringLiteral("variables/live"), true);
            settings.sync();
            necwb::ui::MainWindow mainWindow;
            mainWindow.resize(1400, 900);
            mainWindow.show();
            mainWindow.activateWindow();
            application.processEvents();
            for (auto* action : mainWindow.findChild<QMenu*>(
                     QStringLiteral("recentModelsMenu"))->actions()) {
                // Opening rebuilds the menu, so stop at the action.
                if (action->data().toString() != modelPath) continue;
                action->trigger();
                break;
            }
            application.processEvents();
            const auto wait = [&application](int milliseconds, const std::function<bool()>& done) {
                QElapsedTimer elapsed;
                elapsed.start();
                while (!done() && elapsed.elapsed() < milliseconds) {
                    application.processEvents(QEventLoop::AllEvents, 10);
                    QThread::msleep(5);
                }
                return done();
            };
            auto* deck = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor"));
            auto* slider = mainWindow.findChild<QSlider*>(QStringLiteral("variableSlider"));
            auto* valueBox = mainWindow.findChild<QDoubleSpinBox*>(QStringLiteral("variableValue"));
            auto* undo = mainWindow.findChild<QAction*>(QStringLiteral("undoAction"));
            auto* revert = mainWindow.findChild<QAction*>(QStringLiteral("revertAction"));
            auto* reset = mainWindow.findChild<QToolButton*>(QStringLiteral("variablesReset"));
            auto* tree = mainWindow.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
            auto* runs = mainWindow.findChild<QAbstractItemView*>(QStringLiteral("analysisRunsTable"));
            auto* swr = mainWindow.findChild<QLabel*>(QStringLiteral("resultsSwr"));
            QWidget* xyView = nullptr;
            for (auto* view : mainWindow.findChildren<necwb::ui::GeometryView*>())
                if (xyView == nullptr && view->isVisible()) xyView = view;
            const auto text = [deck] { return deck->toPlainText().toUtf8(); };
            if (deck != nullptr && slider != nullptr && valueBox != nullptr && undo != nullptr
                && revert != nullptr && reset != nullptr && tree != nullptr && runs != nullptr
                && swr != nullptr && xyView != nullptr) {
                const auto runsBefore = runs->model()->rowCount();
                const auto revertOffWhenSaved = !revert->isEnabled() && !reset->isEnabled();
                const auto before = xyView->grab().toImage();
                slider->setSliderDown(true);
                slider->setValue(1000);
                const auto previewed = wait(500, [&] { return xyView->grab().toImage() != before; })
                    && text() == modelText;
                slider->setSliderDown(false);
                application.processEvents();
                const auto oneStep = text().startsWith("SY len=0.625\n")
                    && undo->toolTip() == QStringLiteral("Undo: Edit parameter len");
                // The live solve: band centre before any run, one frequency, RP kept.
                const auto solved = wait(5000, [&] { return swr->text().startsWith(QStringLiteral("1")); });
                const auto inputs = QDir(inputsPath).entryList(QDir::Files);
                QFile input(QDir(inputsPath).filePath(inputs.value(0)));
                const auto inputText = input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray{};
                const auto liveCopy = solved && inputs.size() == 1
                    && inputText.contains("FR 0 1 0 0 146") && inputText.contains("RP 0")
                    && text().startsWith("SY len=0.625\n") && text().contains("FR 0 5 0 0 144 1")
                    && runs->model()->rowCount() == runsBefore;
                // Typed but not applied: the first Ctrl+Z puts it back, the next undoes the drag.
                valueBox->setFocus();
                valueBox->findChild<QLineEdit*>()->setText(QStringLiteral("0.7"));
                QTest::keyClick(valueBox, Qt::Key_Z, Qt::ControlModifier);
                const auto typedText = valueBox->text();
                const auto typingReverted = typedText == QStringLiteral("0.625")
                    && text().startsWith("SY len=0.625\n");
                QTest::keyClick(valueBox, Qt::Key_Z, Qt::ControlModifier);
                application.processEvents();
                const auto undoneFromBox = text().startsWith("SY len=0.5\n");
                tree->setFocus();
                QTest::keyClick(tree, Qt::Key_Y, Qt::ControlModifier);
                application.processEvents();
                const auto redoneFromTree = text().startsWith("SY len=0.625\n");
                // Reset: back to the file's value as one step.
                const auto resetListed = reset->isEnabled()
                    && reset->toolTip().contains(QStringLiteral("len 0.625 → 0.5"));
                reset->click();
                application.processEvents();
                const auto resetOneStep = text() == modelText && !reset->isEnabled()
                    && undo->toolTip() == QStringLiteral("Undo: Reset variables");
                // Revert: discards the drag after asking, and Undo brings it back.
                undo->trigger();
                application.processEvents();
                QTimer::singleShot(0, [] {
                    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                        box->button(QMessageBox::Discard)->click();
                });
                const auto revertOn = revert->isEnabled();
                revert->trigger();
                application.processEvents();
                const auto reverted = text() == modelText && !revert->isEnabled()
                    && undo->toolTip() == QStringLiteral("Undo: Revert to Saved");
                undo->trigger();
                application.processEvents();
                const auto revertUndone = text().startsWith("SY len=0.625\n") && revert->isEnabled();
                // A model never saved has nothing to revert to.
                deck->document()->setModified(false);
                mainWindow.findChild<QAction*>(QStringLiteral("newModelAction"))->trigger();
                application.processEvents();
                deck->appendPlainText(QStringLiteral("CM edited"));
                const auto neverSavedOff = deck->document()->isModified() && !revert->isEnabled();
                variablesLive = neverSavedOff && revertOffWhenSaved && previewed && oneStep && liveCopy
                    && typingReverted && undoneFromBox && redoneFromTree && resetListed
                    && resetOneStep && revertOn && reverted && revertUndone;
                if (!variablesLive) {
                    qWarning() << "variables live" << revertOffWhenSaved << previewed << oneStep
                               << solved << liveCopy << inputs << inputText << typingReverted
                               << typedText << undoneFromBox << redoneFromTree
                               << resetListed << reset->toolTip() << resetOneStep
                               << undo->toolTip() << neverSavedOff << revertOn << reverted << revertUndone;
                }
            }
            if (deck != nullptr) deck->document()->setModified(false);
            mainWindow.close();
        }
        qunsetenv("NECWB_FAKE_SOLVER_INPUTS");
        for (const auto& [key, value] : saved) {
            if (value.isValid()) settings.setValue(key, value);
            else settings.remove(key);
        }
        settings.remove(QStringLiteral("mainWindow/v2"));
        settings.sync();
    }
    if (!variablesLive)
        return NECWB_SMOKE_FAILURE("variables live, undo, reset and revert");
    // A model opened with nothing selected leaves the pane empty: the variables take the
    // free height, all rows shown without a scroll bar of their own.
    NECWB_SMOKE_STEP();
    {
        QSettings settings;
        const auto savedRecent = settings.value(QStringLiteral("files/recentModels"));
        settings.remove(QStringLiteral("variables/folded"));
        QTemporaryDir directory;
        const auto modelPath = directory.filePath(QStringLiteral("variables.nec"));
        QFile modelFile(modelPath);
        if (!directory.isValid() || !modelFile.open(QIODevice::WriteOnly | QIODevice::Text))
            return NECWB_SMOKE_FAILURE("variables model file");
        modelFile.write("SY a=1, b=2, c=3, d=4, e=5, f=6\nGW 1 11 0 -a 0 0 a 0 0.001\nGE 0\n"
            "EX 0 1 6 0 1 0\nFR 0 1 0 0 146 0\nEN\n");
        modelFile.close();
        settings.setValue(QStringLiteral("files/recentModels"), QStringList{modelPath});
        const auto savedReopen = necwb::ui::recentModels::reopenAtStartup();
        necwb::ui::recentModels::setReopenAtStartup(true);
        // Reopened at launch in a small window with nothing selected, then made larger.
        necwb::ui::MainWindow mainWindow;
        mainWindow.resize(1000, 560);
        mainWindow.show();
        application.processEvents();
        mainWindow.reopenLastModel(false);
        application.processEvents();
        necwb::ui::recentModels::setReopenAtStartup(savedReopen);
        mainWindow.resize(1500, 1050);
        application.processEvents();
        auto* section = dynamic_cast<necwb::ui::VariablesSection*>(
            mainWindow.findChild<QWidget*>(QStringLiteral("variablesSection")));
        const auto* pane = mainWindow.findChild<QWidget*>(QStringLiteral("propertiesPane"));
        const auto* scroll = section == nullptr ? nullptr : section->findChild<QScrollArea*>();
        const auto variablesFill = section != nullptr && pane != nullptr && pane->isHidden()
            && scroll != nullptr && scroll->verticalScrollBar()->maximum() == 0
            && section->findChildren<QSlider*>().size() == 6;
        if (!variablesFill)
            qWarning() << "variables fill" << (section ? section->height() : -1)
                       << (section ? section->unfoldedHeight() : -1)
                       << (section ? section->maximumHeight() : -1)
                       << (pane ? pane->isHidden() : false)
                       << (scroll ? scroll->verticalScrollBar()->maximum() : -1)
                       << (section ? section->sizeHint() : QSize());
        if (auto* deck = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor")))
            deck->document()->setModified(false);
        mainWindow.close();
        settings.setValue(QStringLiteral("files/recentModels"), savedRecent);
        if (!variablesFill) return NECWB_SMOKE_FAILURE("variables take the free height");
    }
    // Card tables in the properties pane, the only editors of their items: a group shows its
    // table and folds the variables; ⇔ widens the column to the whole table, at most 65% of the
    // window, and anything else narrows it again; a row's click selects its item, a tree item
    // selects its row, a double-click edits a cell and Return opens nothing; edits keep SY
    // symbols; new cards land before FR and RP; a right-clicked load and a TL picked by two
    // ends show as rows; New empties the pane; the Variables header folds them.
    auto cardTables = false;
    NECWB_SMOKE_STEP();
    {
        QSettings settings;
        const auto savedRecent = settings.value(QStringLiteral("files/recentModels"));
        settings.remove(QStringLiteral("ui/bottomPanel"));
        settings.remove(QStringLiteral("variables/folded"));
        settings.remove(QStringLiteral("mainWindow/v2"));
        QTemporaryDir directory;
        const auto modelPath = directory.filePath(QStringLiteral("cards.nec"));
        QFile modelFile(modelPath);
        if (directory.isValid() && modelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            modelFile.write("SY r=50, v=1\nGW 1 11 0 -0.5 0 0 0.5 0 0.001\n"
                "GW 2 11 0.3 -0.5 0 0.3 0.5 0 0.001\nGE 0\nEX 0 1 6 0 v 0\n"
                "LD 4 1 3 3 r 0 0\nFR 0 1 0 0 146 0\nRP 0 37 73 1000 0 0 5 5\nEN\n");
            modelFile.close();
            settings.setValue(QStringLiteral("files/recentModels"), QStringList{modelPath});
            settings.sync();
            necwb::ui::MainWindow mainWindow;
            mainWindow.resize(1400, 900);
            mainWindow.show();
            application.processEvents();
            for (auto* action : mainWindow.findChild<QMenu*>(
                     QStringLiteral("recentModelsMenu"))->actions()) {
                if (action->data().toString() != modelPath) continue;
                action->trigger();
                break;
            }
            application.processEvents();
            auto* deck = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("necSourceEditor"));
            auto* tree = mainWindow.findChild<QTreeWidget*>(QStringLiteral("modelTree"));
            const auto openedTreeHeight = tree == nullptr ? 0 : tree->height();
            auto* bottomTabs = mainWindow.findChild<QTabBar*>(QStringLiteral("bottomPanelTabs"));
            auto* split = mainWindow.findChild<QSplitter*>(QStringLiteral("workbenchSplit"));
            auto* loads = mainWindow.findChild<QWidget*>(QStringLiteral("cardsLD"));
            auto* sources = mainWindow.findChild<QWidget*>(QStringLiteral("cardsEX"));
            auto* lines = mainWindow.findChild<QWidget*>(QStringLiteral("cardsTL"));
            auto* networks = mainWindow.findChild<QWidget*>(QStringLiteral("cardsNT"));
            auto* expand = mainWindow.findChild<QToolButton*>(QStringLiteral("paneExpandButton"));
            auto* header = mainWindow.findChild<QWidget*>(QStringLiteral("variablesHeader"));
            auto* live = mainWindow.findChild<QCheckBox*>(QStringLiteral("variablesLive"));
            auto* section = mainWindow.findChild<QWidget*>(QStringLiteral("variablesSection"));
            necwb::ui::GeometryView* view = nullptr;
            for (auto* candidate : mainWindow.findChildren<necwb::ui::GeometryView*>())
                if (view == nullptr && candidate->isVisible()) view = candidate;
            const auto text = [deck] { return deck->toPlainText(); };
            const auto selectGroup = [&](const QString& name) {
                for (auto index = 0; index < tree->topLevelItemCount(); ++index)
                    if (tree->topLevelItem(index)->text(0) == name)
                        tree->setCurrentItem(tree->topLevelItem(index));
                application.processEvents();
            };
            const auto pageTitle = [&mainWindow] {
                const auto* title = mainWindow.findChild<QLabel*>(QStringLiteral("propertiesPaneTitle"));
                return title == nullptr ? QString{} : title->text();
            };
            const auto beforeRun = [&](const QString& card) {
                const auto at = text().indexOf(card);
                return at >= 0 && at < text().indexOf(QStringLiteral("FR 0 1"))
                    && at < text().indexOf(QStringLiteral("RP 0"));
            };
            if (deck != nullptr && tree != nullptr && bottomTabs != nullptr && split != nullptr
                && loads != nullptr && sources != nullptr && lines != nullptr && networks != nullptr
                && expand != nullptr && header != nullptr && live != nullptr
                && section != nullptr && view != nullptr) {
                auto* loadTable = loads->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
                auto* sourceTable = sources->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
                auto* lineTable = lines->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
                auto* networkTable = networks->findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
                const auto bottomPanelPlain = bottomTabs->count() == 4;
                // The tree is fitted to whole rows, Validation's studies included.
                auto* modelTree = static_cast<necwb::ui::ModelTree*>(tree);
                auto* convergenceRow = tree->findItems(QStringLiteral("Convergence"),
                    Qt::MatchExactly | Qt::MatchRecursive).value(0);
                const auto treeWholeRows = openedTreeHeight == modelTree->heightForRows(modelTree->fittedRows())
                    && convergenceRow != nullptr
                    && tree->viewport()->rect().contains(tree->visualItemRect(convergenceRow));
                if (!treeWholeRows)
                    qWarning() << "tree rows" << openedTreeHeight << modelTree->fittedRows()
                               << modelTree->heightForRows(modelTree->fittedRows())
                               << (convergenceRow ? tree->visualItemRect(convergenceRow) : QRect{})
                               << tree->viewport()->rect();
                selectGroup(QStringLiteral("Loads"));
                const auto narrowWidth = split->sizes().value(0);
                const auto groupShows = loadTable->isVisible() && loadTable->rowCount() == 1
                    && expand->isVisible() && !expand->isChecked()
                    && section->height() <= header->height() + 2;
                // ⇔ widens the column to show every column; the Ground row narrows it again.
                expand->click();
                application.processEvents();
                const auto wideWidth = split->sizes().value(0);
                const auto widened = wideWidth > narrowWidth
                    && wideWidth <= static_cast<int>(mainWindow.width() * 0.65) + 1
                    && !loadTable->horizontalScrollBar()->isVisible();
                selectGroup(QStringLiteral("Ground"));
                const auto narrowedAgain = split->sizes().value(0) == narrowWidth
                    && !expand->isVisible() && !expand->isChecked() && pageTitle() == QStringLiteral("Ground");
                // Picking a narrow card family after a wide one takes ⇔ away.
                selectGroup(QStringLiteral("Other"));
                auto* families = mainWindow.findChild<QWidget*>(QStringLiteral("cardsOther"))
                    ->findChild<QTreeWidget*>(QStringLiteral("structuredCardFamilies"));
                const auto pickFamily = [&](const QString& prefix) {
                    for (QTreeWidgetItemIterator item(families); *item; ++item)
                        if ((*item)->text(0).startsWith(prefix)) families->setCurrentItem(*item);
                    application.processEvents();
                    return expand->isVisible();
                };
                const auto familyRefits = pickFamily(QStringLiteral("GH")) && !pickFamily(QStringLiteral("XQ"));
                // Right-click Add on the empty Networks group adds an NT before FR and selects its row.
                QTreeWidgetItem* networksGroup = nullptr;
                for (auto index = 0; index < tree->topLevelItemCount(); ++index)
                    if (tree->topLevelItem(index)->text(0) == QStringLiteral("Networks"))
                        networksGroup = tree->topLevelItem(index);
                QTimer::singleShot(0, [] {
                    if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                        menu->actions().front()->trigger();
                        menu->close();
                    }
                });
                emit tree->customContextMenuRequested(tree->visualItemRect(networksGroup).center());
                application.processEvents();
                const auto treeAddOpensForm = beforeRun(QStringLiteral("NT 1 6 2 6 0.02"))
                    && pageTitle() == QStringLiteral("Networks") && networkTable->currentRow() == 0
                    && !networkTable->selectedItems().isEmpty();
                // A wide page that is not a table, Requests, gets ⇔ too.
                const auto* paneWidget = static_cast<necwb::ui::PropertiesPane*>(
                    mainWindow.findChild<QWidget*>(QStringLiteral("propertiesPane")));
                auto* paneScroll = paneWidget->findChild<QScrollArea*>();
                selectGroup(QStringLiteral("Requests"));
                const auto requestsOffered = pageTitle() == QStringLiteral("Requests") && expand->isVisible();
                expand->click();
                application.processEvents();
                const auto requestsWidened = split->sizes().value(0) >= std::min(paneWidget->fullWidth(),
                        static_cast<int>(mainWindow.width() * 0.65))
                    && !paneScroll->horizontalScrollBar()->isVisible();
                // Frequency fits, and the taller Requests page leaves it no scroll bar.
                selectGroup(QStringLiteral("Frequency"));
                const auto pageWidening = requestsOffered && requestsWidened
                    && split->sizes().value(0) == narrowWidth && !expand->isVisible()
                    && !paneScroll->verticalScrollBar()->isVisible();
                selectGroup(QStringLiteral("Loads"));
                // Edit X of the load and the segment of the source: the r and v symbols stay.
                loadTable->item(0, 7)->setText(QStringLiteral("10"));
                application.processEvents();
                sourceTable->item(0, 4)->setText(QStringLiteral("5"));
                application.processEvents();
                const auto symbolsKept = text().contains(QStringLiteral("LD 4 1 3 3 r 10 0"))
                    && text().contains(QStringLiteral("EX 0 1 5 0 v 0"));
                // An expression typed into a load's number cell stays in the deck and shows its
                // value; right-click offers Replace on it and Make Variable on a plain number; a
                // changed variable updates it.
                const auto waitFor = [&](const auto& done) {
                    QElapsedTimer waited;
                    waited.start();
                    while (!done() && waited.elapsed() < 3000) application.processEvents(QEventLoop::AllEvents, 20);
                    return done();
                };
                loadTable->item(0, 8)->setText(QStringLiteral("r * 2"));
                const auto typedKept = waitFor([&] { return text().contains(QStringLiteral("LD 4 1 3 3 r 10 r*2")); })
                    && waitFor([&] { return loadTable->item(0, 8)->text() == QStringLiteral("100"); })
                    && loadTable->item(0, 8)->data(necwb::ui::CellExpressionRole).toString() == QStringLiteral("r*2")
                    && loadTable->item(0, 6)->text() == QStringLiteral("50");
                const auto menuOffers = [&](int column) {
                    QStringList offered;
                    QTimer::singleShot(100, [&offered] {
                        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                            for (const auto* action : menu->actions()) offered << action->text();
                            menu->close();
                        }
                    });
                    emit loadTable->customContextMenuRequested(
                        loadTable->visualItemRect(loadTable->item(0, column)).center());
                    application.processEvents();
                    return offered;
                };
                const auto typedMenu = menuOffers(8);
                const auto plainMenu = menuOffers(7);
                const auto loadMenus = typedMenu == QStringList{QStringLiteral("Change Variable…"),
                        QStringLiteral("Replace With Current Numeric Value")}
                    && plainMenu == QStringList{QStringLiteral("Make Variable…")};
                // The first variable row is r.
                auto* rValue = mainWindow.findChild<QDoubleSpinBox*>(QStringLiteral("variableValue"));
                const auto setR = [&](double value) {
                    rValue->setValue(value);
                    emit rValue->editingFinished();
                    application.processEvents();
                };
                setR(60.0);
                const auto variableFollowed = waitFor([&] { return loadTable->item(0, 8)->text() == QStringLiteral("120"); });
                setR(50.0);
                const auto loadSymbols = typedKept && loadMenus && variableFollowed
                    && waitFor([&] { return loadTable->item(0, 8)->text() == QStringLiteral("100"); });
                if (!loadSymbols)
                    qWarning() << "load symbols" << typedKept << typedMenu << plainMenu << variableFollowed
                               << loadTable->item(0, 8)->text() << text();
                // A load's segment takes a variable or an expression when it comes to a whole
                // number, and refuses one that does not; the type code takes neither.
                const auto segmentMenu = menuOffers(4);
                const auto typeMenu = menuOffers(2);
                loadTable->item(0, 4)->setText(QStringLiteral("r/25+1"));
                const auto segmentTyped = waitFor([&] { return text().contains(QStringLiteral("LD 4 1 r/25+1 3 r 10 r*2")); })
                    && waitFor([&] { return loadTable->item(0, 4)->text() == QStringLiteral("3"); });
                QString refusal;
                QTimer::singleShot(200, [&refusal] {
                    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                        refusal = box->text();
                        box->close();
                    }
                });
                loadTable->item(0, 5)->setText(QStringLiteral("r/20"));
                waitFor([&] { return !refusal.isEmpty(); });
                application.processEvents();
                const auto segmentSymbols = segmentMenu == QStringList{QStringLiteral("Make Variable…")}
                    && typeMenu.isEmpty() && segmentTyped
                    && refusal == QStringLiteral("r/20 cannot be used here: LD field 4, r/20 = 2.5, must be a whole "
                        "number: it is a tag, segment, count or type code")
                    && text().contains(QStringLiteral("LD 4 1 r/25+1 3 r 10 r*2"))
                    && loadTable->item(0, 5)->text() == QStringLiteral("3");
                if (!segmentSymbols)
                    qWarning() << "segment symbols" << segmentMenu << typeMenu << segmentTyped << refusal << text();
                // A click selects the load in the tree and keeps the table; Return opens nothing,
                // and a double-click edits the cell.
                selectGroup(QStringLiteral("Loads"));
                loadTable->selectRow(0);
                application.processEvents();
                const auto selection = tree->currentItem() == nullptr
                        || tree->currentItem()->parent() == nullptr
                    ? QString{} : tree->currentItem()->parent()->text(0);
                const auto rowSelects = selection == QStringLiteral("Loads") && loadTable->isVisible();
                loadTable->setFocus();
                QTest::keyClick(loadTable, Qt::Key_Return);
                application.processEvents();
                const auto editing = [loadTable] {
                    const auto editors = loadTable->viewport()->findChildren<QLineEdit*>();
                    return std::ranges::any_of(editors, [](const QLineEdit* editor) { return editor->isVisible(); });
                };
                const auto returnQuiet = pageTitle() == QStringLiteral("Loads") && loadTable->isVisible()
                    && !editing();
                loadTable->scrollToItem(loadTable->item(0, 7));
                const QPointF cell = loadTable->visualItemRect(loadTable->item(0, 7)).center();
                for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                         QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type, cell, loadTable->viewport()->mapToGlobal(cell), Qt::LeftButton,
                        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(loadTable->viewport(), &event);
                }
                application.processEvents();
                const auto doubleClickEdits = editing() && pageTitle() == QStringLiteral("Loads");
                if (auto* cellEditor = loadTable->viewport()->findChild<QLineEdit*>())
                    QTest::keyClick(cellEditor, Qt::Key_Escape);
                application.processEvents();
                const auto lineOpensForm = returnQuiet && doubleClickEdits && !editing();
                if (!lineOpensForm) qWarning() << "return/double-click" << returnQuiet << doubleClickEdits << pageTitle()
                    << loadTable->viewport()->findChildren<QWidget*>().size() << QApplication::focusWidget()
                    << loadTable->currentRow() << loadTable->currentColumn() << (loadTable->item(0, 7)->flags() & Qt::ItemIsEditable).toInt();
                emit view->excitationSelected(5);
                application.processEvents();
                const auto geometrySelects = sourceTable->currentRow() == 0
                    && pageTitle() == QStringLiteral("Sources");
                // New cards from every table land with the others, before FR and RP.
                for (const auto& [group, table] : std::initializer_list<std::pair<QString, QWidget*>>{
                         {QStringLiteral("Sources"), sources}, {QStringLiteral("Loads"), loads},
                         {QStringLiteral("Trans. lines"), lines}, {QStringLiteral("Networks"), networks}}) {
                    selectGroup(group);
                    table->findChild<QPushButton*>(QStringLiteral("structuredAddCardButton"))->click();
                    application.processEvents();
                }
                const auto tableAddsPlaced = beforeRun(QStringLiteral("EX 0 1 6 0 1 0"))
                    && beforeRun(QStringLiteral("LD 0 1 1 11 0 0 0"))
                    && beforeRun(QStringLiteral("TL 1 6 2 6 50"))
                    && beforeRun(QStringLiteral("NT 1 6 2 6 0.02"));
                // With the table widened, a right-clicked load and a TL picked by its two ends
                // land before FR and show as their rows, the table still widened.
                selectGroup(QStringLiteral("Loads"));
                expand->click();
                application.processEvents();
                emit view->addLoadRequested(2, 6);
                application.processEvents();
                const auto rowIs = [](QTableWidget* table, const QStringList& fields) {
                    const auto row = table->currentRow();
                    if (row < 0 || table->selectedItems().isEmpty()) return false;
                    for (auto field = 0; field < fields.size(); ++field)
                        if (table->item(row, field + 2)->text() != fields[field]) return false;
                    return true;
                };
                const auto loadAdded = beforeRun(QStringLiteral("LD 4 2 6 6 50 0 0"))
                    && pageTitle() == QStringLiteral("Loads")
                    && rowIs(loadTable, {QStringLiteral("4"), QStringLiteral("2"), QStringLiteral("6")})
                    && split->sizes().value(0) > narrowWidth;
                selectGroup(QStringLiteral("Trans. lines"));
                emit view->transmissionLineEndpointRequested(1, 4);
                emit view->transmissionLineEndpointRequested(2, 4);
                application.processEvents();
                const auto linePicked = beforeRun(QStringLiteral("TL 1 4 2 4 50"))
                    && pageTitle() == QStringLiteral("Transmission lines")
                    && rowIs(lineTable, {QStringLiteral("1"), QStringLiteral("4"), QStringLiteral("2"), QStringLiteral("4")})
                    && split->sizes().value(0) > narrowWidth;
                if (!linePicked) qWarning() << "line picked" << pageTitle() << lineTable->currentRow()
                    << (lineTable->currentRow() >= 0 ? lineTable->item(lineTable->currentRow(), 2)->text() : QString{})
                    << split->sizes().value(0) << narrowWidth << text();
                expand->click();
                application.processEvents();
                // An item picked in the tree is its row in its group's table, selected and in view.
                const auto treeSelectsRow = [&](const QString& group, QTableWidget* table, const QString& title) {
                    auto* groupRow = tree->findItems(group, Qt::MatchExactly).value(0);
                    if (groupRow == nullptr || groupRow->childCount() < 2) return false;
                    auto* item = groupRow->child(groupRow->childCount() - 1);
                    tree->setCurrentItem(item);
                    emit tree->itemClicked(item, 0);
                    application.processEvents();
                    const auto row = table->currentRow();
                    const auto picked = pageTitle() == title && table->isVisible()
                        && row == table->rowCount() - 1 && !table->selectedItems().isEmpty()
                        && table->viewport()->rect().intersects(table->visualItemRect(table->item(row, 0)));
                    if (!picked) qWarning() << "tree row" << group << pageTitle() << row << table->rowCount();
                    return picked;
                };
                auto* wireTable = mainWindow.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
                const auto treeRows = treeSelectsRow(QStringLiteral("Wires"), wireTable, QStringLiteral("Wires"))
                    && treeSelectsRow(QStringLiteral("Sources"), sourceTable, QStringLiteral("Sources"))
                    && treeSelectsRow(QStringLiteral("Loads"), loadTable, QStringLiteral("Loads"))
                    && treeSelectsRow(QStringLiteral("Trans. lines"), lineTable, QStringLiteral("Transmission lines"))
                    && treeSelectsRow(QStringLiteral("Networks"), networkTable, QStringLiteral("Networks"));
                // The line under the table explains the selected row: a source's volts and the
                // segment it shares, a wire's diameter; nothing with no row selected.
                const auto noteOf = [&](const QString& page) {
                    const auto* note = mainWindow.findChild<QWidget*>(page)->findChild<QLabel*>(QStringLiteral("cardRowNote"));
                    return note == nullptr ? QStringLiteral("?") : note->text();
                };
                treeSelectsRow(QStringLiteral("Sources"), sourceTable, QStringLiteral("Sources"));
                const auto sourceNote = noteOf(QStringLiteral("cardsEX"));
                selectGroup(QStringLiteral("Wires"));
                wireTable->selectRow(0);
                application.processEvents();
                const auto wireNote = noteOf(QStringLiteral("cardsWires"));
                selectGroup(QStringLiteral("Ground"));
                selectGroup(QStringLiteral("Wires"));
                const auto notesFollow = sourceNote.startsWith(QString::fromUtf8("1 V ∠ 0°"))
                    && sourceNote.contains(QStringLiteral(" · "))
                    && wireNote.startsWith(QString::fromUtf8("Ø 0.002 m")) && wireNote.contains(QStringLiteral("long"))
                    && noteOf(QStringLiteral("cardsWires")).isEmpty();
                if (!notesFollow) qWarning() << "row notes" << sourceNote << wireNote << noteOf(QStringLiteral("cardsWires"));
                // With nothing in the pane, the header folds the variables to one line with Live
                // still there, and back.
                // A new name typed into a wire cell asks for its value under the cell, starting
                // at the number it replaces: Create adds it after the SY cards, Esc changes nothing.
                selectGroup(QStringLiteral("Wires"));
                auto* wireCells = mainWindow.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
                const auto typeName = [&](int row, int column, const QString& name, bool create) {
                    QString guess;
                    QTimer::singleShot(100, [&guess, create] {
                        auto* box = qobject_cast<QDialog*>(QApplication::activePopupWidget());
                        if (box == nullptr) return;
                        guess = box->findChild<QLineEdit*>(QStringLiteral("symbolValue"))->text();
                        if (create) box->findChild<QPushButton*>(QStringLiteral("symbolCreate"))->click();
                        else box->reject();
                    });
                    wireCells->item(row, column)->setText(name);
                    application.processEvents();
                    return guess;
                };
                const auto created = typeName(1, 2, QStringLiteral("len"), true) == QStringLiteral("0.3")
                    && text().startsWith(QStringLiteral("SY r=50, v=1\nSY len=0.3\nGW 1"))
                    && text().contains(QStringLiteral("GW 2 11 len -0.5"));
                const auto deckBefore = text();
                const auto cancelled = typeName(0, 4, QStringLiteral("w"), false) == QStringLiteral("0")
                    && text() == deckBefore && wireCells->item(0, 4)->text() == QStringLiteral("0");
                const auto namesTyped = created && cancelled;
                // In the narrow column a new name's box, Create too, stays inside the table.
                selectGroup(QStringLiteral("Sources"));
                auto realColumn = -1;
                for (auto column = 0; column < sourceTable->columnCount(); ++column)
                    if (sourceTable->horizontalHeaderItem(column)->text() == QStringLiteral("Real V")) realColumn = column;
                QRect boxShown, createShown, viewportShown, cellShown;
                auto boxPastCell = false;
                if (realColumn >= 0) {
                    sourceTable->scrollToItem(sourceTable->item(0, realColumn));
                    viewportShown = QRect(sourceTable->viewport()->mapToGlobal(QPoint(0, 0)),
                        sourceTable->viewport()->size());
                    const auto cell = sourceTable->visualItemRect(sourceTable->item(0, realColumn));
                    cellShown = QRect(sourceTable->viewport()->mapToGlobal(cell.topLeft()), cell.size());
                    const auto cellLeft = cellShown.left();
                    QTimer::singleShot(100, [&] {
                        auto* box = qobject_cast<QDialog*>(QApplication::activePopupWidget());
                        if (box == nullptr) return;
                        auto* create = box->findChild<QPushButton*>(QStringLiteral("symbolCreate"));
                        boxShown = box->frameGeometry();
                        createShown = QRect(create->mapToGlobal(QPoint(0, 0)), create->size());
                        boxPastCell = cellLeft + box->width() > viewportShown.right();
                        box->reject();
                    });
                    sourceTable->item(0, realColumn)->setText(QStringLiteral("newname/2"));
                    application.processEvents();
                }
                // Inside the table from side to side, and under the cell, or over it when the
                // screen has no room below; a short table leaves the box hanging past its rows.
                const auto across = [&viewportShown](const QRect& shown) {
                    return shown.left() >= viewportShown.left() && shown.right() <= viewportShown.right();
                };
                const auto boxInside = !boxShown.isEmpty() && across(boxShown) && across(createShown)
                    && (boxShown.top() > cellShown.bottom() || boxShown.bottom() < cellShown.top())
                    && !text().contains(QStringLiteral("newname"));
                if (!boxInside || !boxPastCell)
                    qWarning() << "symbol box" << boxShown << createShown << viewportShown << cellShown << boxPastCell;
                // Clicking rows in Wires and Sources selects their items in the tree, which keeps
                // the height it opened with, and the table stays where it was.
                const auto clickRows = [&](QTableWidget* table) {
                    const auto at = table->mapTo(&mainWindow, QPoint(0, 0));
                    auto still = true;
                    for (auto row = 0; row < table->rowCount(); ++row) {
                        QTest::mouseClick(table->viewport(), Qt::LeftButton, {},
                            table->visualItemRect(table->item(row, 0)).center());
                        application.processEvents();
                        still = still && table->isVisible() && table->mapTo(&mainWindow, QPoint(0, 0)) == at
                            && tree->height() == openedTreeHeight && tree->currentItem() != nullptr
                            && tree->currentItem()->parent() != nullptr;
                    }
                    return still;
                };
                selectGroup(QStringLiteral("Wires"));
                const auto wiresStill = clickRows(wireCells);
                selectGroup(QStringLiteral("Sources"));
                const auto sourcesStill = clickRows(sourceTable);
                const auto treeKeepsHeight = wiresStill && sourcesStill && openedTreeHeight > 0;
                if (!treeKeepsHeight) qWarning() << "tree height" << wiresStill << sourcesStill << openedTreeHeight << tree->height();
                selectGroup(QStringLiteral("Wires"));
                // A wire picked in the table is selected in the tree; clicking it there keeps the table.
                wireCells->selectRow(0);
                application.processEvents();
                auto* wireRow = tree->findItems(QStringLiteral("Wire 1"), Qt::MatchExactly | Qt::MatchRecursive).value(0);
                const auto tableKept = wireRow != nullptr && tree->currentItem() == wireRow
                    && pageTitle() == QStringLiteral("Wires");
                if (wireRow != nullptr) emit tree->itemClicked(wireRow, 0);
                application.processEvents();
                const auto wireOpened = tableKept && pageTitle() == QStringLiteral("Wires")
                    && wireCells->currentRow() == 0;
                if (!wireOpened) qWarning() << "wire click" << tableKept << pageTitle();
                selectGroup(QStringLiteral("Deck"));
                QMouseEvent click(QEvent::MouseButtonRelease, QPointF(5, 5), header->mapToGlobal(QPointF(5, 5)),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(header, &click);
                application.processEvents();
                const auto folded = section->height() <= header->height() + 2 && live->isVisible()
                    && settings.value(QStringLiteral("variables/folded")).toBool();
                QApplication::sendEvent(header, &click);
                application.processEvents();
                const auto unfolded = section->height() > header->height() + 2
                    && !settings.value(QStringLiteral("variables/folded")).toBool();
                // New empties the pane, and the Variables bar sits right under the tree, folded
                // or not, with the spare height below it.
                deck->document()->setModified(false);
                mainWindow.findChild<QAction*>(QStringLiteral("newModelAction"))->trigger();
                application.processEvents();
                const auto* pane = mainWindow.findChild<QWidget*>(QStringLiteral("propertiesPane"));
                const auto underTree = [&] {
                    return section->geometry().top() == tree->geometry().bottom() + 1
                        && section->geometry().bottom() < section->parentWidget()->height() - 1;
                };
                const auto unfoldedUnder = underTree();
                QApplication::sendEvent(header, &click);
                application.processEvents();
                const auto foldedUnder = underTree() && section->height() <= header->height() + 2;
                QApplication::sendEvent(header, &click);
                application.processEvents();
                const auto newEmpties = pane->isHidden() && tree->selectedItems().isEmpty()
                    && unfoldedUnder && foldedUnder;
                if (!newEmpties)
                    qWarning() << "new" << pane->isHidden() << tree->selectedItems().size() << unfoldedUnder
                               << foldedUnder << tree->geometry() << section->geometry();
                cardTables = bottomPanelPlain && groupShows && widened && narrowedAgain && pageWidening
                    && familyRefits && treeAddOpensForm
                    && symbolsKept && loadSymbols && segmentSymbols && rowSelects && lineOpensForm && geometrySelects
                    && tableAddsPlaced && loadAdded && linePicked && namesTyped && boxInside && boxPastCell
                    && treeKeepsHeight && wireOpened && folded && unfolded && treeWholeRows && treeRows
                    && notesFollow && newEmpties;
                if (!cardTables) {
                    qWarning() << "card tables" << bottomPanelPlain << groupShows << widened << pageWidening
                               << familyRefits << treeAddOpensForm
                               << narrowWidth << wideWidth << narrowedAgain << symbolsKept
                               << rowSelects << selection << lineOpensForm << geometrySelects
                               << pageTitle() << tableAddsPlaced << loadAdded << linePicked
                               << namesTyped << folded << unfolded << text();
                }
            }
            if (deck != nullptr) deck->document()->setModified(false);
            mainWindow.close();
        }
        settings.setValue(QStringLiteral("files/recentModels"), savedRecent);
        settings.remove(QStringLiteral("ui/bottomPanel"));
        settings.remove(QStringLiteral("variables/folded"));
        settings.remove(QStringLiteral("mainWindow/v2"));
        settings.sync();
    }
    if (!cardTables)
        return NECWB_SMOKE_FAILURE("card tables in the pane, expand, placement and folding");
    if (!variableSliderEdits)
        return NECWB_SMOKE_FAILURE("variable slider edits the SY card");
    if (!sourcePlacement)
        return NECWB_SMOKE_FAILURE("a source placed along its wire follows its segmentation");
    if (!segmentationPreviewApplied)
        return NECWB_SMOKE_FAILURE("automatic segmentation skips symbolic fields and applies its preview");
    if (!wirePaneKeepsSymbols)
        return NECWB_SMOKE_FAILURE("cell edits keep SY symbols and exact values");
    if (!openModelKeepsDeckClosed)
        return NECWB_SMOKE_FAILURE("open model keeps NEC Deck closed");
    if (!leftColumnCappedAtContent)
        return NECWB_SMOKE_FAILURE("left column capped at control panel width");
    if (!bottomPanelOpensAtMinimumSize)
        return NECWB_SMOKE_FAILURE("bottom panel opens at minimum window size");
    if (!consolidatedSetupNavigation)
        return NECWB_SMOKE_FAILURE("consolidated setup navigation");
    if (!linkedParameterDeleted)
        return NECWB_SMOKE_FAILURE("delete linked parameter");
    if (!resultsColumnNavigation)
        return NECWB_SMOKE_FAILURE("results column navigation");
    if (!bottomPanelNavigation)
        return NECWB_SMOKE_FAILURE("bottom panel navigation");
    if (!validationWindowNavigation)
        return NECWB_SMOKE_FAILURE("validation window navigation");
    if (!deckInCenter)
        return NECWB_SMOKE_FAILURE("NEC Deck in the center");
    if (!geometryInMainWindow)
        return NECWB_SMOKE_FAILURE("geometry editor in main window");
    if (!undoFollowsFocusChanges)
        return NECWB_SMOKE_FAILURE("undo follows focus between geometry and deck");
    if (!dialogDensityIsIncremental)
        return NECWB_SMOKE_FAILURE("incremental dialog workspace density");
    if (!setupWindowNavigation)
        return NECWB_SMOKE_FAILURE("setup window navigation");
    if (!optimizationWindowNavigation)
        return NECWB_SMOKE_FAILURE("optimization window navigation");
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
            result.radiation.push_back({14.2, static_cast<double>(theta),
                static_cast<double>(phi), gain + 0.1, -999.99, gain + 0.1});
        }
    }
    result.radiation.push_back({14.2, 75.0, 60.0, 20.0, -999.99, 20.0});
    for (auto phi = 0; phi < 360; ++phi) {
        auto sample = necwb::analysis::RadiationSample{
            14.2, 62.0, static_cast<double>(phi), -10.0, -999.99, -10.0};
        sample.patternIndex = 1;
        result.radiation.push_back(sample);
    }
    QSettings plotSettings;
    const auto previousSwrScale = plotSettings.value(QStringLiteral("results/swrScaleModeV2"));
    plotSettings.setValue(QStringLiteral("results/swrScaleModeV2"), 0);
    plotSettings.sync();
    necwb::ui::SweepPlotsView view;
    view.resize(900, 700);
    view.setResults(necwb::ui::ResultData::make(result), QStringLiteral("test-run"));
    auto referenceResult = result;
    for (auto& feedpoint : referenceResult.feedpoints)
        feedpoint.impedance += std::complex<double>{3.0, -2.0};
    view.setReferenceResults(necwb::ui::ResultData::make(referenceResult),
        QStringLiteral("20261019-123456-789-very-long-reference-run-identity"));
    view.setSelectedFrequency(14.2);
    view.show();
    application.processEvents();

    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    view.render(&painter);
    painter.end();
    auto* impedanceSweepPlot = view.findChild<QWidget*>(QStringLiteral("impedanceSweepPlot"));
    auto* swrSweepPlot = view.findChild<QWidget*>(QStringLiteral("swrSweepPlot"));
    auto* swrScaleControl = view.findChild<QComboBox*>(QStringLiteral("swrScaleControl"));
    auto* sweepShowReference = view.findChild<QCheckBox*>(
        QStringLiteral("sweepShowReference"));
    const auto sweepReferenceDefaultsHidden = sweepShowReference != nullptr
        && !sweepShowReference->isChecked();
    if (sweepShowReference != nullptr) sweepShowReference->setChecked(true);
    application.processEvents();
    const auto frequencyTickLabels = impedanceSweepPlot == nullptr
        ? QStringList{} : impedanceSweepPlot->property("xAxisTickLabels").toStringList();
    const auto frequencyTicksDistinct = frequencyTickLabels.size() > 1
        && std::adjacent_find(frequencyTickLabels.begin(), frequencyTickLabels.end())
            == frequencyTickLabels.end();
    const auto engineeringAxes = impedanceSweepPlot != nullptr && swrSweepPlot != nullptr
        && impedanceSweepPlot->property("dualAxes").toBool()
        && impedanceSweepPlot->property("leftAxisScale").toInt() == 1
        && impedanceSweepPlot->property("rightAxisScale").toInt() == 0
        && impedanceSweepPlot->property("leftAxisMinimum").toDouble() > 0.0
        && impedanceSweepPlot->property("rightAxisMinimum").toDouble() <= 0.0
        && impedanceSweepPlot->property("rightAxisMaximum").toDouble() >= 0.0
        && impedanceSweepPlot->property("leftAxisTickCount").toInt() >= 2
        && impedanceSweepPlot->property("rightAxisTickCount").toInt() >= 2
        && impedanceSweepPlot->property("referenceSeriesCount").toInt() == 2
        && impedanceSweepPlot->property("referenceSeriesAreGhosted").toBool()
        && impedanceSweepPlot->property("plotLeftMargin").toDouble() >= 90.0
        && impedanceSweepPlot->property("plotRightMargin").toDouble() >= 90.0
        && !swrSweepPlot->property("dualAxes").toBool()
        && swrSweepPlot->property("leftAxisScale").toInt() == 1
        && swrSweepPlot->property("leftAxisMinimum").toDouble() == 1.0
        && swrSweepPlot->property("referenceSeriesCount").toInt() == 1
        && swrSweepPlot->property("referenceSeriesAreGhosted").toBool()
        && frequencyTicksDistinct
        && swrSweepPlot->property("leftAxisTickCount").toInt() >= 3
        && impedanceSweepPlot->property("legendFits").toBool()
        && swrSweepPlot->property("legendFits").toBool()
        && std::ranges::none_of(
            impedanceSweepPlot->property("legendLabels").toStringList(),
            [](const QString& label) { return label.contains(QStringLiteral("20261019")); })
        && std::ranges::none_of(
            swrSweepPlot->property("legendLabels").toStringList(),
            [](const QString& label) { return label.contains(QStringLiteral("20261019")); });
    if (sweepShowReference != nullptr) sweepShowReference->setChecked(false);
    application.processEvents();
    const auto sweepReferenceCanHide = impedanceSweepPlot != nullptr
        && impedanceSweepPlot->property("referenceSeriesCount").toInt() == 0
        && swrSweepPlot != nullptr
        && swrSweepPlot->property("referenceSeriesCount").toInt() == 0;
    if (sweepShowReference != nullptr) sweepShowReference->setChecked(true);
    referenceResult.referenceImpedanceOhms = 75.0;
    view.setReferenceResults(necwb::ui::ResultData::make(referenceResult), QStringLiteral("different-z0"));
    application.processEvents();
    // The overlay's SWR is recomputed at the current Z0 rather than hidden.
    if (impedanceSweepPlot->property("referenceSeriesCount").toInt() != 2
        || swrSweepPlot->property("referenceSeriesCount").toInt() != 1
        || !swrSweepPlot->property("title").toString().contains(QStringLiteral("Z0 50 Ω"))) {
        return NECWB_SMOKE_FAILURE("reference plots with differing Z0");
    }
    view.setReferenceImpedance(200.0);
    application.processEvents();
    if (!swrSweepPlot->property("title").toString().contains(QStringLiteral("Z0 200 Ω")))
        return NECWB_SMOKE_FAILURE("SWR plot follows a Z0 change");
    view.setReferenceImpedance(50.0);
    if (sweepShowReference == nullptr || !sweepReferenceDefaultsHidden || !sweepReferenceCanHide
        || swrScaleControl == nullptr || swrScaleControl->count() != 4
        || swrScaleControl->currentText() != QStringLiteral("Logarithmic")) {
        return NECWB_SMOKE_FAILURE("SWR scale control initialization");
    }
    swrScaleControl->setCurrentIndex(2);
    QPainter alternateScalePainter(&image);
    view.render(&alternateScalePainter);
    alternateScalePainter.end();
    const auto alternateScalesWork = swrSweepPlot->property("scaleMode").toInt() == 2
        && swrSweepPlot->property("leftAxisScale").toInt() == 0
        && swrSweepPlot->property("leftAxisMinimum").toDouble() == 1.0
        && swrSweepPlot->property("leftAxisMaximum").toDouble() == 3.0;
    if (previousSwrScale.isValid()) {
        plotSettings.setValue(QStringLiteral("results/swrScaleModeV2"), previousSwrScale);
    } else {
        plotSettings.remove(QStringLiteral("results/swrScaleModeV2"));
    }
    plotSettings.sync();
    if (!alternateScalesWork) return NECWB_SMOKE_FAILURE("alternate SWR scales");
    swrScaleControl->setCurrentIndex(0);
    view.resize(900, 520);
    application.processEvents();
    QPainter stackedPainter(&image);
    view.render(&stackedPainter);
    stackedPainter.end();
    const auto stackedPlotsShareSpace
        = std::abs(impedanceSweepPlot->height() - swrSweepPlot->height()) <= 2
        && swrSweepPlot->height() >= 200
        && impedanceSweepPlot->property("plotLeftMargin").toDouble()
            == swrSweepPlot->property("plotLeftMargin").toDouble()
        && impedanceSweepPlot->property("plotRightMargin").toDouble()
            == swrSweepPlot->property("plotRightMargin").toDouble()
        && swrSweepPlot->property("leftAxisVisibleTickLabels").toInt() >= 2
        && swrSweepPlot->property("leftAxisVisibleTickLabels").toInt()
            <= swrSweepPlot->property("leftAxisTickCount").toInt();
    if (!stackedPlotsShareSpace) return NECWB_SMOKE_FAILURE("stacked sweep plot layout");
    necwb::ui::CandidatePlotsView candidatePlots;
    candidatePlots.resize(760, 360);
    const std::vector<necwb::ui::CandidatePlotPoint> candidatePoints{
        {1, 10.0, {.score = 1.2, .swr = 1.5, .swrComponent = 0.9,
            .resistanceComponent = 0.2, .reactanceComponent = 0.1}},
        {2, 11.0, {.score = 1.6, .swr = 1.8, .swrComponent = 1.1,
            .resistanceComponent = 0.3, .reactanceComponent = 0.2}},
        {0, 9.0, {.score = 2.0, .swr = 2.5, .swrComponent = 1.5,
            .resistanceComponent = 0.3, .reactanceComponent = 0.2}},
    };
    candidatePlots.setCandidates(QStringLiteral("LENGTH"), QStringLiteral("ft"),
        candidatePoints, 1);
    candidatePlots.show();
    application.processEvents();
    QImage candidatePlotImage(candidatePlots.size(), QImage::Format_ARGB32_Premultiplied);
    candidatePlotImage.fill(Qt::transparent);
    QPainter candidatePlotPainter(&candidatePlotImage);
    candidatePlots.render(&candidatePlotPainter);
    candidatePlotPainter.end();
    auto* candidateScorePlot = candidatePlots.findChild<QWidget*>(
        QStringLiteral("optimizationCandidateScorePlot"));
    const auto candidatePlotValid = !candidatePlotImage.isNull()
        && candidateScorePlot != nullptr
        && candidateScorePlot->property("pointCount").toInt() == 3
        && candidateScorePlot->property("xValuesAscending").toBool()
        && candidateScorePlot->property("bestX").toDouble() == 10.0
        && candidateScorePlot->property("leftAxisMinimum").toDouble() == 0.0
        && candidateScorePlot->property("leftAxisMaximum").toDouble() >= 2.0;
    necwb::ui::ResultsSummaryView resultsSummary;
    resultsSummary.resize(700, 420);
    resultsSummary.setResults(result, QStringLiteral("test-run"));
    resultsSummary.setSelectedFrequency(14.1);
    auto* historicalInputPanel = resultsSummary.findChild<QWidget*>(
        QStringLiteral("historicalInputSnapshotPanel"));
    const auto historicalInputInitiallyHidden = historicalInputPanel != nullptr
        && historicalInputPanel->isHidden();
    resultsSummary.setHistoricalInputSnapshot(QStringLiteral("CM authored\nEN\n"),
        QStringLiteral("CM generated\nEN\n"));
    auto* historicalInputSource = resultsSummary.findChild<QComboBox*>(
        QStringLiteral("historicalInputSnapshotSource"));
    auto* historicalInputText = resultsSummary.findChild<QPlainTextEdit*>(
        QStringLiteral("historicalInputSnapshotText"));
    const auto authoredInputShown = historicalInputPanel != nullptr
        && !historicalInputPanel->isHidden() && historicalInputSource != nullptr
        && historicalInputSource->count() == 2 && historicalInputText != nullptr
        && historicalInputText->isReadOnly()
        && historicalInputText->toPlainText().contains(QStringLiteral("authored"));
    if (historicalInputSource != nullptr) historicalInputSource->setCurrentIndex(1);
    const auto generatedInputShown = historicalInputText != nullptr
        && historicalInputText->toPlainText().contains(QStringLiteral("generated"));
    resultsSummary.show();
    application.processEvents();
    QImage summaryImage(resultsSummary.size(), QImage::Format_ARGB32_Premultiplied);
    summaryImage.fill(Qt::transparent);
    QPainter summaryPainter(&summaryImage);
    resultsSummary.render(&summaryPainter);
    summaryPainter.end();
    necwb::ui::AverageGainResultsView averageGainResults;
    QAction runAverageGain(QStringLiteral("Run Average Gain Test"), &averageGainResults);
    auto averageGainTriggered = false;
    QObject::connect(&runAverageGain, &QAction::triggered,
        [&averageGainTriggered] { averageGainTriggered = true; });
    averageGainResults.setRunAction(&runAverageGain);
    averageGainResults.resize(700, 420);
    averageGainResults.setResult(necwb::analysis::assessAverageGain(0.98, 1.0), 14.1,
        necwb::analysis::AverageGainEnvironment::FreeSpace, 4.0,
        QStringLiteral("Model: test.nec · Run: AGT"));
    averageGainResults.show();
    application.processEvents();
    auto* averageGainTable = averageGainResults.findChild<QTableWidget*>();
    auto* runAverageGainButton = averageGainResults.findChild<QToolButton*>(
        QStringLiteral("runAverageGainFromValidationButton"));
    if (runAverageGainButton != nullptr) runAverageGainButton->click();
    if (averageGainTable == nullptr || averageGainTable->item(1, 1) == nullptr
        || averageGainTable->item(1, 1)->text() != QStringLiteral("0.980")
        || runAverageGainButton == nullptr || !averageGainTriggered) {
        return NECWB_SMOKE_FAILURE("average gain results");
    }
    QImage averageGainImage(averageGainResults.size(), QImage::Format_ARGB32_Premultiplied);
    averageGainImage.fill(Qt::transparent);
    QPainter averageGainPainter(&averageGainImage);
    averageGainResults.render(&averageGainPainter);
    averageGainPainter.end();
    necwb::ui::ConvergenceWorkspace convergence;
    convergence.resize(900, 620);
    convergence.show();
    application.processEvents();
    auto* runConvergence = convergence.findChild<QPushButton*>(
        QStringLiteral("runConvergenceStudyButton"));
    auto* convergenceHistoricalBanner = convergence.findChild<QWidget*>(
        QStringLiteral("convergenceHistoricalBanner"));
    auto* convergenceReturn = convergence.findChild<QPushButton*>(
        QStringLiteral("convergenceReturnToCurrentWorkButton"));
    const auto convergenceHistoryControlsValid = runConvergence != nullptr
        && runConvergence->text() == QStringLiteral("Run Convergence Study")
        && convergenceHistoricalBanner != nullptr && !convergenceHistoricalBanner->isVisible()
        && convergenceReturn != nullptr;
    if (!convergenceHistoryControlsValid)
        return NECWB_SMOKE_FAILURE("convergence history controls");
    convergence.setContext(QStringLiteral("GW 1 1 0 0 0 0.001 0 0 0.00000001\n"
                                          "GE 0\n"
                                          "EX 0 1 1 0 1 0\n"
                                          "FR 0 1 0 0 1 0\n"
                                          "EN\n"),
        QStringLiteral("tiny-wire.nec"), QStringLiteral("nec2"),
        QCoreApplication::applicationFilePath(), 120, true);
    runConvergence->click();
    application.processEvents();
    application.processEvents();
    auto* convergenceResults = convergence.findChild<QTableWidget*>(
        QStringLiteral("convergenceResultsTable"));
    auto* convergenceAssessmentDetail = convergence.findChild<QLabel*>(
        QStringLiteral("convergenceAssessmentDetail"));
    const auto* convergenceAssessment = convergenceResults == nullptr
        ? nullptr : convergenceResults->item(0, 7);
    if (convergenceAssessment == nullptr
        || convergenceAssessment->text() != QStringLiteral("Not run — segment < 0.001 λ")
        || !convergenceAssessment->toolTip().contains(QStringLiteral("0.001 wavelength"))
        || convergenceAssessment->icon().isNull() || convergenceAssessmentDetail == nullptr) {
        return NECWB_SMOKE_FAILURE("convergence compact assessment");
    }
    convergenceResults->selectRow(0);
    application.processEvents();
    if (!convergenceAssessmentDetail->isVisible()
        || !convergenceAssessmentDetail->text().contains(QStringLiteral("0.001 wavelength"))) {
        return NECWB_SMOKE_FAILURE("convergence assessment detail");
    }
    QImage convergenceImage(convergence.size(), QImage::Format_ARGB32_Premultiplied);
    convergenceImage.fill(Qt::transparent);
    QPainter convergencePainter(&convergenceImage);
    convergence.render(&convergencePainter);
    convergencePainter.end();
    QSettings{}.remove(QStringLiteral("optimization/candidateReviewSizeV1"));
    QSettings{}.remove(QStringLiteral("optimization/candidateReviewGeometryV1"));
    necwb::ui::OptimizationWorkspace optimization;
    optimization.resize(1000, 700);
    optimization.show();
    application.processEvents();
    const auto optimizationSource = QStringLiteral("SY LONG_FT=95\n"
                       "SY FT=0.3048\n"
                       "SY HALF=LONG_FT*FT/2\n"
                       "GW 1 21 -HALF 0 10 HALF 0 10 0.001\n"
                       "GE 0\nEX 0 1 11 0 1 0\nFR 0 1 0 0 7.15 0\nEN\n");
    optimization.setContext(optimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    application.processEvents();
    auto* optimizationConfiguration = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationConfigurationPanel"));
    auto* optimizationWorkspaceSplitter = optimization.findChild<QSplitter*>(
        QStringLiteral("optimizationWorkspaceSplitter"));
    auto* optimizationResultsPanel = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationResultsPanel"));
    auto* optimizationConfigurationScrollArea = optimization.findChild<QScrollArea*>(
        QStringLiteral("optimizationConfigurationScrollArea"));
    auto* optimizationSetupContent = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationSetupContent"));
    auto* optimizationVariableSection = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationVariableSection"));
    auto* optimizationFrequencySection = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationFrequencySection"));
    auto* optimizationObjectiveSection = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationObjectiveSection"));
    auto* optimizationResultsTable = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationResultsTable"));
    auto* optimizationCandidateHint = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationCandidateHint"));
    auto* optimizationResultsSplitter = optimization.findChild<QSplitter*>(
        QStringLiteral("optimizationResultsSplitter"));
    auto* optimizationCandidatePlots = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationCandidatePlots"));
    auto* optimizationRun = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationRun"));
    auto* optimizationCandidateDetailsWindow = topLevelDialog(
        QStringLiteral("optimizationCandidateDetailsWindow"));
    const auto optimizationCandidateReviewDefaultSize =
        optimizationCandidateDetailsWindow == nullptr
        ? QSize{} : optimizationCandidateDetailsWindow->size();
    const auto availableScreenSize = QApplication::primaryScreen() == nullptr
        ? QSize{860, 620}
        : QApplication::primaryScreen()->availableGeometry().size();
    const auto expectedCandidateReviewSize = QSize{860, 620}.boundedTo(
        QSize{qMax(1, qRound(availableScreenSize.width() * 0.90)),
            qMax(1, qRound(availableScreenSize.height() * 0.88))});
    const auto optimizationMargins = optimization.layout()->contentsMargins();
    const auto candidateReviewMargins = optimizationCandidateDetailsWindow == nullptr
        ? QMargins{} : optimizationCandidateDetailsWindow->layout()->contentsMargins();
    auto* optimizationComparePanel = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QWidget*>(
            QStringLiteral("optimizationComparePanel"));
    auto* optimizationBaselineStatus = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QLabel*>(
            QStringLiteral("optimizationBaselineStatus"));
    auto* optimizationCandidateDetails = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QTableWidget*>(
            QStringLiteral("optimizationCandidateDetails"));
    auto* optimizationCandidateDetailViews = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QTabWidget*>(
            QStringLiteral("optimizationCandidateDetailViews"));
    auto* optimizationCandidateReferenceControl = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QCheckBox*>(
            QStringLiteral("sweepShowReference"));
    auto* optimizationCompareWithStartingModel =
        optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QCheckBox*>(
            QStringLiteral("optimizationCompareWithStartingModel"));
    const auto candidateComparisonDefaultsOff =
        optimizationCompareWithStartingModel != nullptr
        && !optimizationCompareWithStartingModel->isChecked();
    auto* optimizationCandidateDirectionalPlots = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QWidget*>(
            QStringLiteral("optimizationCandidateDirectionalPlots"));
    auto* optimizationApplyCandidate = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QPushButton*>(
            QStringLiteral("optimizationApplyCandidate"));
    auto* optimizationCompareBreakdown = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QLabel*>(
            QStringLiteral("optimizationCompareBreakdown"));
    auto* optimizationApplyCandidateAndRun = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QPushButton*>(
            QStringLiteral("optimizationApplyCandidateAndRun"));
    auto* optimizationCandidateHeader = optimizationCandidateDetailsWindow == nullptr
        ? nullptr : optimizationCandidateDetailsWindow->findChild<QLabel*>(
            QStringLiteral("optimizationCandidateDetailLabel"));
    auto* optimizationCompareTabs = optimizationComparePanel == nullptr
        ? nullptr : optimizationComparePanel->findChild<QTabWidget*>();
    auto* optimizationVariables = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationVariablesTable"));
    auto* optimizationParameterSettings = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationParameterSettings"));
    // The frequency summary keeps its own line breaks and shows each line whole.
    const auto summaryFitsText = [](const QLabel* label) {
        return label != nullptr && !label->wordWrap() && label->text().contains(QLatin1Char('\n'))
            && label->width() >= label->sizeHint().width()
            && label->height() >= label->sizeHint().height();
    };
    auto* optimizationMakeVariable = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationMakeVariable"));
    auto* optimizationObjectiveSettings = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationObjectiveSettings"));
    auto* optimizationObjectivePreset = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationObjectivePreset"));
    auto* optimizationPresetSwrLimit = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationPresetSwrLimit"));
    auto* optimizationDirectionSetupPanel = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationDirectionSetupPanel"));
    auto* optimizationObjectiveCriteria = optimization.findChild<QTableWidget*>(
        QStringLiteral("optimizationObjectiveCriteria"));
    auto* optimizationSwrWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationSwrWeight"));
    auto* optimizationResistanceWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationResistanceWeight"));
    auto* optimizationResistanceTarget = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationResistanceTarget"));
    auto* optimizationReactanceWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationReactanceWeight"));
    auto* optimizationReactanceTarget = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationReactanceTarget"));
    auto* optimizationSearchBudgetLabel = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationSearchBudgetLabel"));
    auto* optimizationStudySummary = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationStudySummary"));
    auto* optimizationFrequencyMode = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationFrequencyMode"));
    auto* optimizationFrequencySummary = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationFrequencySummary"));
    auto* optimizationEditFrequencies = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationEditFrequencies"));
    auto* optimizationFrequencyDialog = optimization.findChild<QDialog*>(
        QStringLiteral("optimizationFrequencyDialog"));
    auto* optimizationSwrGoal = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationGoal0"));
    auto* optimizationGainGoal = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationGoal3"));
    auto* optimizationSwrAggregation = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationAggregation0"));
    auto* optimizationGainAggregation = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationAggregation3"));
    auto* optimizationSwrValue = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationObjectiveValue0"));
    auto* optimizationResetObjectiveDefaults = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationResetObjectiveDefaults"));
    auto* optimizationObjectiveSummary = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationObjectiveSummary"));
    auto* optimizationObjectiveWarning = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationObjectiveWarning"));
    auto* optimizationObjectiveExplanation = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationObjectiveExplanation"));
    auto* optimizationEditObjective = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationEditObjective"));
    auto* optimizationObjectiveDialog = optimization.findChild<QDialog*>(
        QStringLiteral("optimizationObjectiveDialog"));
    auto* optimizationDirectionalFrequencyNote = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationDirectionalFrequencyNote"));
    auto* optimizationDirectionSetupSummary = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationDirectionSetupSummary"));
    auto* optimizationChooseDirection = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationChooseDirection"));
    auto* optimizationFindMaximumGain = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationFindMaximumGain"));
    auto* optimizationUseLatestPatternPeak = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationUseLatestPatternPeak"));
    auto* optimizationLatestPatternPeakStatus = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationLatestPatternPeakStatus"));
    auto* optimizationFrontToBackWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationFrontToBackWeight"));
    auto* optimizationFrontToRearWeight = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationFrontToRearWeight"));
    auto* optimizationFrequencyTable = optimization.findChild<QListWidget*>(
        QStringLiteral("optimizationFrequencyTable"));
    auto* optimizationAddAmateurBand = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationAddAmateurBand"));
    auto* optimizationClearFrequencies = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationClearFrequencies"));
    auto* optimizationExplicitFrequencies = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationExplicitFrequencyPanel"));
    auto* optimizationContinuousFrequencies = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationContinuousFrequencyPanel"));
    auto* optimizationContinuousStart = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationContinuousStart"));
    auto* optimizationContinuousStop = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationContinuousStop"));
    auto* optimizationContinuousStep = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationContinuousStep"));
    auto* optimizationWorkload = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationWorkload"));
    auto* optimizationApplyBest = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationApplyBest"));
    auto* optimizationSearchMethods = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationSearchMethod"));
    auto* optimizationScoreToleranceLabel = optimization.findChild<QLabel*>(
        QStringLiteral("optimizationScoreToleranceLabel"));
    auto* optimizationResetSearchDefaults = optimization.findChild<QPushButton*>(
        QStringLiteral("optimizationResetSearchDefaults"));
    auto* optimizationAdaptiveMaximum = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationAdaptiveMaximumEvaluations"));
    auto* optimizationCandidateCount = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationCandidateCount"));
    auto* optimizationAdaptiveScoreTolerance = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationAdaptiveScoreTolerance"));
    auto* optimizationDifferentialEvolutionSettings = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationDifferentialEvolutionSettings"));
    auto* optimizationDifferentialEvolutionPopulation = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionPopulation"));
    auto* optimizationDifferentialEvolutionGenerations = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionGenerations"));
    auto* optimizationDifferentialEvolutionMutation = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionMutation"));
    auto* optimizationDifferentialEvolutionCrossover = optimization.findChild<QDoubleSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionCrossover"));
    auto* optimizationDifferentialEvolutionScoreTolerance =
        optimization.findChild<QDoubleSpinBox*>(
            QStringLiteral("optimizationDifferentialEvolutionScoreTolerance"));
    auto* optimizationDifferentialEvolutionSeed = optimization.findChild<QSpinBox*>(
        QStringLiteral("optimizationDifferentialEvolutionSeed"));
    if (optimizationResultsTable != nullptr) optimizationResultsTable->setRowCount(1);
    optimization.setContext(optimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    auto parameterizationHelpInvoked = false;
    optimization.setParameterizationHelpCallback(
        [&parameterizationHelpInvoked] { parameterizationHelpInvoked = true; });
    if (optimizationMakeVariable != nullptr)
        optimizationMakeVariable->click();
    const auto optimizerResultsPreserved = optimizationResultsTable != nullptr
        && optimizationResultsTable->rowCount() == 1;
    if (optimizationResultsTable != nullptr) optimizationResultsTable->setRowCount(0);
    if (optimizationFrequencyMode != nullptr) optimizationFrequencyMode->setCurrentIndex(1);
    QString amateurBandSummary;
    QTimer::singleShot(0, &optimization, [&optimization, &amateurBandSummary] {
        auto* dialog = optimization.findChild<QDialog*>(
            QStringLiteral("optimizationAmateurBandDialog"));
        if (dialog == nullptr) return;
        auto* fortyMeters = dialog->findChild<QCheckBox*>(
            QStringLiteral("optimizationAmateurBandCheck2"));
        auto* addSelected = dialog->findChild<QPushButton*>(
            QStringLiteral("optimizationAddSelectedBands"));
        if (fortyMeters != nullptr) fortyMeters->setChecked(true);
        if (auto* summary = dialog->findChild<QLabel*>(
                QStringLiteral("optimizationAmateurBandSummary")))
            amateurBandSummary = summary->text();
        if (addSelected != nullptr) addSelected->click();
    });
    if (optimizationAddAmateurBand != nullptr) optimizationAddAmateurBand->click();
    const auto& fortyMeterPreset = necwb::analysis::amateurBandPresets()[2];
    const auto fortyMeterPoints = necwb::analysis::frequencyPlanPoints({
        .mode = necwb::analysis::FrequencyPlanMode::Explicit, .pointsMHz = {},
        .ranges = {{fortyMeterPreset.startMHz, fortyMeterPreset.endMHz,
            fortyMeterPreset.stepMHz}}}).size();
    if (amateurBandSummary != QStringLiteral("1 bands · %1 frequency points").arg(fortyMeterPoints)) {
        qWarning() << "amateur band summary" << amateurBandSummary;
        return NECWB_SMOKE_FAILURE("optimization amateur band summary");
    }
    QStringList selectedFrequencies;
    if (optimizationFrequencyTable != nullptr) {
        for (auto row = 0; row < optimizationFrequencyTable->count(); ++row)
            selectedFrequencies.push_back(optimizationFrequencyTable->item(row)->text());
    }
    const auto refreshedOptimizationSource = QStringLiteral("CM refreshed model\n")
        + optimizationSource;
    optimization.setContext(refreshedOptimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    auto sameModelFrequenciesPreserved = optimizationFrequencyMode != nullptr
        && optimizationFrequencyMode->currentData().toInt() == 1
        && optimizationFrequencyTable != nullptr
        && optimizationFrequencyTable->count() == selectedFrequencies.size();
    for (auto row = 0; sameModelFrequenciesPreserved
         && row < optimizationFrequencyTable->count(); ++row) {
        sameModelFrequenciesPreserved = optimizationFrequencyTable->item(row)->text()
            == selectedFrequencies.at(row);
    }
    necwb::ui::AnalysisRunStore optimizationRunStore;
    auto optimizationBaseline = optimizationRunStore.create(QStringLiteral("nec2"),
        QStringLiteral("historical.nec"), QStringLiteral("optimization-baseline"));
    optimizationBaseline.status = QStringLiteral("Completed");
    optimizationRunStore.save(optimizationBaseline);
    auto historicalOptimization = optimizationRunStore.create(QStringLiteral("nec2"),
        QStringLiteral("historical.nec"), QStringLiteral("optimization-session"));
    historicalOptimization.status = QStringLiteral("Completed");
    optimizationRunStore.save(historicalOptimization);
    auto historicalCandidate = optimizationRunStore.create(QStringLiteral("nec2"),
        QStringLiteral("historical.nec"), QStringLiteral("optimization-candidate"),
        historicalOptimization.id);
    historicalCandidate.status = QStringLiteral("Completed");
    optimizationRunStore.save(historicalCandidate);
    auto historicalCandidateTwo = optimizationRunStore.create(QStringLiteral("nec2"),
        QStringLiteral("historical.nec"), QStringLiteral("optimization-candidate"),
        historicalOptimization.id);
    historicalCandidateTwo.status = QStringLiteral("Completed");
    optimizationRunStore.save(historicalCandidateTwo);
    QFile historicalMetadata(QDir(historicalOptimization.directory).filePath(
        QStringLiteral("optimization-session.json")));
    const auto historicalMetadataWritten = historicalMetadata.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && historicalMetadata.write(QStringLiteral(
            "{\"frequencyMode\":\"explicit\",\"frequenciesMHz\":[7,14],"
            "\"variable\":\"LONG_FT\",\"variables\":[{\"name\":\"LONG_FT\","
            "\"resolvedValue\":95,\"minimum\":80,\"maximum\":110,"
            "\"tolerance\":0.01},{\"name\":\"FT\",\"resolvedValue\":0.3048,"
            "\"minimum\":0.25,\"maximum\":0.35,\"tolerance\":0.001}],"
            "\"baselineRunId\":\"%1\"}").arg(optimizationBaseline.id).toUtf8()) > 0;
    historicalMetadata.close();
    QFile candidateMetadata(QDir(historicalCandidate.directory).filePath(
        QStringLiteral("optimization.json")));
    const auto candidateMetadataWritten = candidateMetadata.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && candidateMetadata.write(
            "{\"variable\":\"LONG_FT\",\"value\":90,\"variables\":[{"
            "\"name\":\"LONG_FT\",\"value\":90},{\"name\":\"FT\",\"value\":0.3}],"
            "\"objective\":\"worst-point-across-frequencies\","
            "\"referenceImpedance\":50,\"swrWeight\":1,\"resistanceWeight\":1,"
            "\"resistanceTargetOhms\":50}") > 0;
    candidateMetadata.close();
    QFile candidateMetadataTwo(QDir(historicalCandidateTwo.directory).filePath(
        QStringLiteral("optimization.json")));
    const auto candidateMetadataTwoWritten = candidateMetadataTwo.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && candidateMetadataTwo.write(
            "{\"variable\":\"LONG_FT\",\"value\":92,\"variables\":[{"
            "\"name\":\"LONG_FT\",\"value\":92},{\"name\":\"FT\",\"value\":0.31}],"
            "\"objective\":\"worst-point-across-frequencies\","
            "\"referenceImpedance\":50,\"swrWeight\":1,\"resistanceWeight\":1,"
            "\"resistanceTargetOhms\":50}") > 0;
    candidateMetadataTwo.close();
    const QByteArray candidateOutput =
        " FREQUENCY : 7.0000E+00 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n"
        " FREQUENCY : 1.4000E+01 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 11 1.0E+00 0.0E+00 1.0E-02 0.0E+00 7.5E+01 1.5E+01 0 0 5.0E-03\n";
    QFile candidateOutputFile(QDir(historicalCandidate.directory).filePath(
        QStringLiteral("model.out")));
    const auto candidateOutputWritten = candidateOutputFile.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && candidateOutputFile.write(candidateOutput) == candidateOutput.size();
    candidateOutputFile.close();
    QFile candidateOutputFileTwo(QDir(historicalCandidateTwo.directory).filePath(
        QStringLiteral("model.out")));
    const auto candidateOutputTwoWritten = candidateOutputFileTwo.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && candidateOutputFileTwo.write(candidateOutput) == candidateOutput.size();
    candidateOutputFileTwo.close();
    QFile baselineOutputFile(QDir(optimizationBaseline.directory).filePath(
        QStringLiteral("model.out")));
    const auto baselineOutputWritten = baselineOutputFile.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && baselineOutputFile.write(candidateOutput) == candidateOutput.size();
    baselineOutputFile.close();
    const auto historicalFrequencySelectionLoaded = historicalMetadataWritten
        && candidateMetadataWritten && candidateMetadataTwoWritten
        && candidateOutputWritten && candidateOutputTwoWritten && baselineOutputWritten
        && optimization.loadSession(historicalOptimization.id)
        && optimizationFrequencyTable != nullptr
        && optimizationFrequencyTable->count() == 2;
    // Sessions written before the variable table recorded one sweep variable with
    // top-level bounds, and candidates could use the retired SWR-at-frequency objective.
    auto legacyOptimization = optimizationRunStore.create(QStringLiteral("nec2"),
        QStringLiteral("legacy.nec"), QStringLiteral("optimization-session"));
    legacyOptimization.status = QStringLiteral("Completed");
    optimizationRunStore.save(legacyOptimization);
    auto legacyCandidate = optimizationRunStore.create(QStringLiteral("nec2"),
        QStringLiteral("legacy.nec"), QStringLiteral("optimization-candidate"),
        legacyOptimization.id);
    legacyCandidate.status = QStringLiteral("Completed");
    optimizationRunStore.save(legacyCandidate);
    QFile legacyMetadata(QDir(legacyOptimization.directory).filePath(
        QStringLiteral("optimization-session.json")));
    const auto legacyMetadataWritten = legacyMetadata.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && legacyMetadata.write(
            "{\"version\":4,\"variable\":\"HALF\",\"minimum\":4,\"maximum\":6,"
            "\"parameterTolerance\":0.05,\"candidateCount\":5,"
            "\"frequencyMode\":\"explicit\",\"frequenciesMHz\":[14.2]}") > 0;
    legacyMetadata.close();
    QFile legacyCandidateMetadata(QDir(legacyCandidate.directory).filePath(
        QStringLiteral("optimization.json")));
    const auto legacyCandidateWritten = legacyCandidateMetadata.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && legacyCandidateMetadata.write(
            "{\"variable\":\"HALF\",\"value\":5,\"objective\":\"swr-at-frequency\","
            "\"targetFrequencyMHz\":14.2,\"referenceImpedance\":50,\"swrWeight\":1,"
            "\"forwardThetaDegrees\":80,\"forwardPhiDegrees\":10,"
            "\"directionalEvaluationMode\":2}") > 0;
    legacyCandidateMetadata.close();
    QFile legacyOutput(QDir(legacyCandidate.directory).filePath(QStringLiteral("model.out")));
    const auto legacyOutputWritten = legacyOutput.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && legacyOutput.write(candidateOutput) == candidateOutput.size();
    legacyOutput.close();
    necwb::ui::OptimizationWorkspace legacySessionWorkspace;
    auto* legacySummary = legacySessionWorkspace.findChild<QLabel*>(
        QStringLiteral("optimizationStudySummary"));
    auto* legacyResults = legacySessionWorkspace.findChild<QTableWidget*>(
        QStringLiteral("optimizationResultsTable"));
    const auto legacySessionLoaded = legacyMetadataWritten && legacyCandidateWritten
        && legacyOutputWritten
        && legacySessionWorkspace.loadSession(legacyOptimization.id)
        && legacySummary != nullptr
        && legacySummary->text().contains(QStringLiteral("Variables: HALF"))
        && legacySummary->text().contains(QStringLiteral("Objective: SWR"))
        && legacyResults != nullptr && legacyResults->rowCount() == 1;
    QFile cachedCandidateMetadata(QDir(historicalCandidate.directory).filePath(
        QStringLiteral("optimization.json")));
    const auto historicalCandidateResultCached = cachedCandidateMetadata.open(
            QIODevice::ReadOnly)
        && QJsonDocument::fromJson(cachedCandidateMetadata.readAll()).object().contains(
            QStringLiteral("objectiveEvaluation"));
    const auto historicalSetupHidden = historicalFrequencySelectionLoaded
        && optimizationConfiguration != nullptr
        && optimizationConfiguration->isHidden();
    auto* workspaceCandidateScorePlot = optimization.findChild<QWidget*>(
        QStringLiteral("optimizationCandidateScorePlot"));
    const auto historicalObjectiveBreakdownLoaded = historicalFrequencySelectionLoaded
        && workspaceCandidateScorePlot != nullptr
        && workspaceCandidateScorePlot->minimumHeight() >= 240
        && workspaceCandidateScorePlot->property("pointCount").toInt() == 2
        && workspaceCandidateScorePlot->property("seriesCount").toInt() == 3
        && workspaceCandidateScorePlot->property("xAxisLabel").toString()
            == QStringLiteral("Evaluation Number");
    const auto historicalBaselineLoaded = historicalFrequencySelectionLoaded
        && optimizationBaselineStatus != nullptr
        && optimizationBaselineStatus->text().contains(
            QStringLiteral("saved with this optimization session"));
    if (optimizationCompareWithStartingModel != nullptr)
        optimizationCompareWithStartingModel->click();
    application.processEvents();
    const auto historicalBaselineDeltaLoaded = historicalBaselineLoaded
        && optimizationCompareBreakdown != nullptr
        && optimizationCompareBreakdown->text().contains(
            QStringLiteral("Comparison with starting model"));
    const auto candidateScoreTooltip = optimizationResultsTable == nullptr
        || optimizationResultsTable->item(0, 1) == nullptr
        ? QString{} : optimizationResultsTable->item(0, 1)->toolTip();
    if (optimizationResultsTable != nullptr)
        optimizationResultsTable->horizontalHeader()->sectionClicked(1);
    application.processEvents();
    const auto numericCandidateSortingEnabled = optimizationResultsTable != nullptr
        && optimizationResultsTable->horizontalHeader()->isSortIndicatorShown()
        && optimizationResultsTable->horizontalHeader()->sortIndicatorSection() == 1
        && optimizationResultsTable->horizontalHeader()->sortIndicatorOrder()
            == Qt::AscendingOrder
        && candidateScoreTooltip.contains(QStringLiteral("Full objective score:"));
    if (optimizationResultsTable != nullptr)
        optimizationResultsTable->horizontalHeader()->sectionClicked(9);
    application.processEvents();
    const auto informationalCandidateColumnsStayUnsorted = optimizationResultsTable != nullptr
        && optimizationResultsTable->horizontalHeader()->sortIndicatorSection() == 1;
    optimization.leaveHistoricalSession();
    auto activeFrequenciesRestored = optimizationFrequencyMode != nullptr
        && optimizationFrequencyMode->currentData().toInt() == 1
        && optimizationFrequencyTable != nullptr
        && optimizationFrequencyTable->count() == selectedFrequencies.size();
    for (auto row = 0; activeFrequenciesRestored
         && row < optimizationFrequencyTable->count(); ++row) {
        activeFrequenciesRestored = optimizationFrequencyTable->item(row)->text()
            == selectedFrequencies.at(row);
    }
    auto appliedCandidateValues = false;
    optimization.setApplyParameterCallback(
        [&optimization, &refreshedOptimizationSource, &appliedCandidateValues](
            const std::vector<std::pair<QString, double>>& values) {
            appliedCandidateValues = values.size() == 2
                && values.front().first == QStringLiteral("LONG_FT")
                && values.front().second == 90.0
                && values[1].first == QStringLiteral("FT")
                && values[1].second == 0.3;
            auto appliedSource = refreshedOptimizationSource;
            appliedSource.replace(QStringLiteral("LONG_FT=95"), QStringLiteral("LONG_FT=90"));
            appliedSource.replace(QStringLiteral("FT=0.3048"), QStringLiteral("FT=0.3"));
            optimization.setContext(appliedSource, QStringLiteral("symbol-units.nec"),
                QStringLiteral("nec2"), QCoreApplication::applicationFilePath(), 120, true);
            return true;
        });
    optimization.setApplyAndRunCallback([](const auto&) { return true; });
    optimization.setBaselineRunCallback([](const auto&, const auto&) { return true; });
    optimization.setContext(refreshedOptimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"),
        QCoreApplication::applicationFilePath(), 120, true);
    const auto candidateRowWithValue = [optimizationResultsTable](QString value) {
        if (optimizationResultsTable == nullptr) return -1;
        for (auto row = 0; row < optimizationResultsTable->rowCount(); ++row) {
            const auto* item = optimizationResultsTable->item(row, 0);
            if (item != nullptr && item->text().contains(value)) return row;
        }
        return -1;
    };
    const auto firstCandidateRow = candidateRowWithValue(QStringLiteral("90.000"));
    const auto secondCandidateRow = candidateRowWithValue(QStringLiteral("92.000"));
    optimizationResultsTable->setCurrentCell(firstCandidateRow, 0);
    optimizationResultsTable->cellDoubleClicked(firstCandidateRow, 0);
    application.processEvents();
    auto* candidateImpedancePlot = optimizationComparePanel->findChild<QWidget*>(
        QStringLiteral("impedanceSweepPlot"));
    const auto candidateDetailsLoaded = optimizationCandidateDetails->rowCount() == 2
        && candidateImpedancePlot != nullptr
        && candidateImpedancePlot->property("seriesCount").toInt() > 0
        && optimizationCandidateReferenceControl != nullptr
        && optimizationCandidateReferenceControl->isHidden()
        && optimizationCompareWithStartingModel != nullptr
        && optimizationCompareWithStartingModel->isChecked()
        && optimizationCompareBreakdown != nullptr
        && optimizationCandidateDetailViews->currentIndex() == 0
        && !optimizationCompareTabs->isTabVisible(2)
        && optimizationCompareTabs->tabToolTip(2).contains(
            QStringLiteral("no directional objective data"))
        && optimizationApplyCandidate->isEnabled()
        && optimizationApplyCandidateAndRun->isEnabled();
    const auto firstCandidateHeader = optimizationCandidateHeader == nullptr
        ? QString{} : optimizationCandidateHeader->text();
    if (secondCandidateRow >= 0)
        optimizationResultsTable->setCurrentCell(secondCandidateRow, 0);
    application.processEvents();
    const auto visibleReviewTracksSelection = optimizationCandidateHeader != nullptr
        && optimizationCandidateHeader->text() != firstCandidateHeader
        && optimizationCandidateHeader->text().contains(QStringLiteral("92.000"));
    if (firstCandidateRow >= 0)
        optimizationResultsTable->setCurrentCell(firstCandidateRow, 0);
    application.processEvents();
    if (optimizationObjectivePreset != nullptr)
        optimizationObjectivePreset->setCurrentText(
            QStringLiteral("Maximize Peak Gain in Cut"));
    necwb::analysis::AnalysisResult candidateDirectionPattern;
    candidateDirectionPattern.radiation = {
        {.frequencyMHz = 7.15, .thetaDegrees = 55.0, .phiDegrees = 0.0,
            .totalGainDb = 6.2},
        {.frequencyMHz = 7.15, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .totalGainDb = -999.99},
    };
    optimization.setLatestAnalysisResult(candidateDirectionPattern);
    application.processEvents();
    optimizationApplyCandidate->click();
    application.processEvents();
    const auto candidateApplyStatusReported = std::ranges::any_of(
        optimization.findChildren<QLabel*>(), [](const auto* label) {
            return label->text().contains(
                QStringLiteral("baseline will be rebuilt"));
        });
    const auto candidateApplyPreservesDirection = optimizationRun != nullptr
        && optimizationRun->isEnabled()
        && optimizationRun->text() == QStringLiteral("Rebuild Baseline && Optimize")
        && !optimizationRun->toolTip().contains(
            QStringLiteral("Direction setup required"))
        && candidateApplyStatusReported;
    const auto candidateApplyPreservesDetails = candidateDetailsLoaded
        && visibleReviewTracksSelection && appliedCandidateValues
        && optimizationResultsTable->rowCount() == 2
        && optimizationCandidateDetails->rowCount() == 2
        && candidateImpedancePlot->property("seriesCount").toInt() > 0;
    if (!candidateApplyPreservesDirection)
        return NECWB_SMOKE_FAILURE("candidate apply preserves direction");
    if (optimizationObjectivePreset != nullptr)
        optimizationObjectivePreset->setCurrentText(
            QStringLiteral("Keep SWR Below Limit"));
    application.processEvents();
    optimizationCandidateDetailsWindow->hide();
    optimization.restoreCandidateDetailsWindow();
    application.processEvents();
    const auto hiddenCandidateDetailsRestored = optimizationCandidateDetailsWindow->isVisible();
    optimizationCandidateDetailsWindow->showMinimized();
    application.processEvents();
    optimization.restoreCandidateDetailsWindow();
    application.processEvents();
    const auto minimizedCandidateDetailsRestored = optimizationCandidateDetailsWindow->isVisible()
        && !optimizationCandidateDetailsWindow->isMinimized();
    optimizationCandidateDetailsWindow->resize(700, 500);
    application.processEvents();
    optimizationCandidateDetailsWindow->close();
    application.processEvents();
    optimizationCandidateDetailsWindow->hide();
    QSettings{}.remove(QStringLiteral("optimization/candidateReviewSizeV1"));
    QSettings{}.remove(QStringLiteral("optimization/candidateReviewGeometryV1"));
    NECWB_SMOKE_STEP();
    {
        necwb::ui::OptimizationWorkspace persistenceWorkspace;
    }
    QSettings candidateReviewSettings;
    candidateReviewSettings.sync();
    const auto savedCandidateReviewSize = candidateReviewSettings.value(
        QStringLiteral("optimization/candidateReviewSizeV1")).toSize();
    const auto savedCandidateReviewGeometry = candidateReviewSettings.value(
        QStringLiteral("optimization/candidateReviewGeometryV1")).toByteArray();
    const auto candidateReviewGeometryPersisted =
        candidateReviewSettings.status() == QSettings::NoError
        && (!savedCandidateReviewGeometry.isEmpty() || savedCandidateReviewSize.isValid());
    optimization.setContext(refreshedOptimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, false);
    optimizationRunStore.removeGroup(
        historicalOptimization.id, historicalOptimization.directory);
    QTimer::singleShot(0, &optimization, [&optimization] {
        auto* dialog = optimization.findChild<QDialog*>(
            QStringLiteral("optimizationFrequencyDialog"));
        auto* mode = optimization.findChild<QComboBox*>(
            QStringLiteral("optimizationFrequencyMode"));
        if (mode != nullptr) mode->setCurrentIndex(2);
        if (dialog != nullptr) dialog->reject();
    });
    if (optimizationEditFrequencies != nullptr) optimizationEditFrequencies->click();
    const auto frequencyCancelRestored = optimizationFrequencyMode != nullptr
        && optimizationFrequencyMode->currentData().toInt() == 1
        && optimizationFrequencyTable != nullptr
        && optimizationFrequencyTable->count() == selectedFrequencies.size();
    optimization.resize(760, 560);
    application.processEvents();
    const auto frequencyControlsStructured = optimizationExplicitFrequencies != nullptr
        && optimizationAddAmateurBand != nullptr
        && optimizationExplicitFrequencies->isAncestorOf(optimizationAddAmateurBand);
    if (optimizationFrequencyDialog != nullptr) optimizationFrequencyDialog->show();
    application.processEvents();
    const auto frequencyShortcuts = optimizationFrequencyTable == nullptr
        ? QList<QShortcut*>{} : optimizationFrequencyTable->findChildren<QShortcut*>();
    const auto hasDeleteShortcut = std::ranges::any_of(frequencyShortcuts, [](const auto* shortcut) {
        return shortcut->key() == QKeySequence(Qt::Key_Delete);
    });
    const auto hasBackspaceShortcut = std::ranges::any_of(
        frequencyShortcuts, [](const auto* shortcut) {
            return shortcut->key() == QKeySequence(Qt::Key_Backspace);
        });
    if (optimizationFrequencyMode != nullptr) optimizationFrequencyMode->setCurrentIndex(2);
    if (optimizationContinuousStart != nullptr) optimizationContinuousStart->setValue(14.0);
    if (optimizationContinuousStop != nullptr) optimizationContinuousStop->setValue(14.35);
    if (optimizationContinuousStep != nullptr) optimizationContinuousStep->setValue(0.01);
    application.processEvents();
    const auto continuousSweepValid = optimizationContinuousFrequencies != nullptr
        && optimizationContinuousFrequencies->isVisible()
        && optimizationExplicitFrequencies != nullptr
        && !optimizationExplicitFrequencies->isVisible()
        && optimizationWorkload != nullptr
        && optimizationWorkload->text().contains(
            QStringLiteral("7 candidates × 36 frequencies"));
    if (optimizationFrequencyDialog != nullptr) optimizationFrequencyDialog->hide();
    QTimer::singleShot(0, &optimization, [&optimization] {
        auto* dialog = optimization.findChild<QDialog*>(
            QStringLiteral("optimizationObjectiveDialog"));
        auto* goal = optimization.findChild<QComboBox*>(
            QStringLiteral("optimizationGoal0"));
        auto* reference = optimization.findChild<QDoubleSpinBox*>(
            QStringLiteral("optimizationReferenceImpedance"));
        if (goal != nullptr) goal->setCurrentIndex(1);
        if (reference != nullptr) reference->setValue(75.0);
        if (dialog != nullptr) dialog->reject();
    });
    if (optimizationEditObjective != nullptr) optimizationEditObjective->click();
    const auto objectiveCancelRestored = optimizationSwrGoal != nullptr
        && optimizationSwrGoal->currentText() == QStringLiteral("Good Enough ≤")
        && optimizationObjectivePreset != nullptr
        && optimizationObjectivePreset->currentText()
            == QStringLiteral("Keep SWR Below Limit")
        && optimizationObjectiveSummary != nullptr
        && optimizationObjectiveSummary->text().contains(QStringLiteral("50.000"));
    auto directionalPresetReadinessValid = false;
    auto objectivePresetsValid = optimizationObjectivePreset != nullptr
        && optimizationObjectivePreset->count() == 8
        && optimizationPresetSwrLimit != nullptr
        && optimizationPresetSwrLimit->value() == 2.0
        && optimizationObjectiveCriteria != nullptr
        && optimizationObjectiveCriteria->isHidden();
    if (optimizationObjectivePreset != nullptr) {
        optimizationObjectivePreset->setCurrentText(
            QStringLiteral("Minimize Worst-Case SWR"));
        application.processEvents();
        objectivePresetsValid = objectivePresetsValid
            && optimizationSwrGoal != nullptr
            && optimizationSwrGoal->currentText() == QStringLiteral("Minimize")
            && optimizationSwrAggregation != nullptr
            && optimizationSwrAggregation->currentText() == QStringLiteral("Worst Point")
            && optimizationPresetSwrLimit->isHidden()
            && optimizationObjectiveSummary->text().contains(
                QStringLiteral("Minimize the highest SWR"));
        optimizationObjectivePreset->setCurrentText(
            QStringLiteral("Keep SWR Below Limit"));
        application.processEvents();
        objectivePresetsValid = objectivePresetsValid
            && optimizationObjectiveSummary->text().contains(
                QStringLiteral("Accept any candidate"));
        optimizationObjectivePreset->setCurrentText(QStringLiteral("Maximize Peak Gain in Cut"));
        application.processEvents();
        objectivePresetsValid = objectivePresetsValid
            && optimizationPresetSwrLimit->isHidden()
            && optimizationDirectionSetupPanel != nullptr
            && !optimizationDirectionSetupPanel->isHidden()
            && optimizationObjectiveSummary->text().contains(
                QStringLiteral("Maximize Peak Gain in Cut"));
        directionalPresetReadinessValid = optimizationDirectionSetupSummary != nullptr
            && optimizationDirectionSetupSummary->text().contains(
                QStringLiteral("Direction setup required"))
            && optimizationLatestPatternPeakStatus != nullptr
            && optimizationLatestPatternPeakStatus->text().contains(
                QStringLiteral("Use Find Sampled Peak or Choose"))
            && optimizationRun != nullptr && !optimizationRun->isEnabled()
            && optimizationRun->toolTip().contains(
                QStringLiteral("Direction setup required"))
            && optimizationObjectiveWarning != nullptr
            && !optimizationObjectiveWarning->isHidden()
            && optimizationObjectiveWarning->text().contains(
                QStringLiteral("Direction setup required"));
        optimizationObjectivePreset->setCurrentText(QStringLiteral("Keep SWR Below Limit"));
        application.processEvents();
    }
    auto goalAwareBandEvaluationValid = false;
    if (optimizationSwrGoal != nullptr && optimizationSwrAggregation != nullptr) {
        optimizationSwrGoal->setCurrentIndex(1);
        application.processEvents();
        const auto maximizeWorst = optimizationSwrAggregation->currentText()
                == QStringLiteral("Worst Point")
            && optimizationSwrAggregation->currentData().toInt()
                == static_cast<int>(necwb::analysis::OptimizationAggregation::Minimum);
        optimizationSwrGoal->setCurrentIndex(2);
        application.processEvents();
        const auto targetWorst = optimizationSwrAggregation->currentText()
                == QStringLiteral("Worst Error")
            && optimizationSwrAggregation->currentData().toInt()
                == static_cast<int>(necwb::analysis::OptimizationAggregation::Maximum)
            && optimizationSwrAggregation->toolTip().contains(
                QStringLiteral("largest absolute error"));
        if (optimizationResetObjectiveDefaults != nullptr)
            optimizationResetObjectiveDefaults->click();
        application.processEvents();
        goalAwareBandEvaluationValid = maximizeWorst && targetWorst
            && !optimizationSwrGoal->toolTip().isEmpty()
            && !optimizationSwrAggregation->toolTip().isEmpty()
            && optimizationSwrGoal->currentText() == QStringLiteral("Minimize")
            && optimizationSwrAggregation->currentText() == QStringLiteral("Worst Point")
            && optimizationSwrAggregation->currentData().toInt()
                == static_cast<int>(necwb::analysis::OptimizationAggregation::Maximum)
            && optimizationObjectiveExplanation != nullptr
            && optimizationObjectiveExplanation->text().contains(
                QStringLiteral("Minimize the highest SWR"));
    }
    if (optimizationFrontToBackWeight != nullptr)
        optimizationFrontToBackWeight->setValue(1.0);
    application.processEvents();
    const auto patternRunPrerequisiteVisible = optimizationUseLatestPatternPeak != nullptr
        && !optimizationUseLatestPatternPeak->isEnabled()
        && optimizationLatestPatternPeakStatus != nullptr
        && optimizationLatestPatternPeakStatus->text().contains(
            QStringLiteral("Run the active model"));
    if (optimizationFrontToBackWeight != nullptr)
        optimizationFrontToBackWeight->setValue(0.0);
    auto requestedMaximumGainFrequency = 0.0;
    optimization.setFindMaximumGainCallback(
        [&requestedMaximumGainFrequency](double frequencyMHz) {
            requestedMaximumGainFrequency = frequencyMHz;
            return false;
        });
    optimization.setModelValid(true);
    if (optimizationFindMaximumGain != nullptr)
        optimizationFindMaximumGain->click();
    optimization.setModelValid(false);
    const auto maximumGainActionValid = optimizationFindMaximumGain != nullptr
        && requestedMaximumGainFrequency > 0.0
        && optimizationFindMaximumGain->text() == QStringLiteral("Find Sampled Peak (5° grid)…")
        && optimizationFindMaximumGain->toolTip().contains(
            QStringLiteral("does not start optimization or modify the NEC model"));
    necwb::analysis::AnalysisResult latestPattern;
    latestPattern.radiation = {
        {.frequencyMHz = 7.15, .thetaDegrees = 55.0, .phiDegrees = 0.0,
            .totalGainDb = 6.2},
        {.frequencyMHz = 7.15, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .totalGainDb = -999.99},
    };
    optimization.setLatestAnalysisResult(latestPattern);
    application.processEvents();
    const auto latestPatternPeakAutoSeeded = optimizationDirectionSetupSummary != nullptr
        && optimizationDirectionSetupSummary->text().contains(
            QStringLiteral("elevation 35.000°"));
    const auto automaticDirectionVisible = optimizationDirectionSetupSummary != nullptr
        && optimizationDirectionSetupSummary->text().contains(
            QStringLiteral("strongest gain · elevation 35.000°"));
    auto directionalPresetAutoSeeded = false;
    auto directionalPresetReadyText = false;
    auto directionalPresetPeakAngle = false;
    if (optimizationObjectivePreset != nullptr) {
        optimizationObjectivePreset->setCurrentText(QStringLiteral("Maximize Peak Gain in Cut"));
        application.processEvents();
        directionalPresetReadyText = optimizationDirectionSetupSummary != nullptr
            && optimizationDirectionSetupSummary->text().contains(
                QStringLiteral("Direction ready"));
        directionalPresetPeakAngle = optimizationDirectionSetupSummary != nullptr
            && optimizationDirectionSetupSummary->text().contains(
                QStringLiteral("elevation 35.000°"));
        directionalPresetAutoSeeded = directionalPresetReadyText
            && directionalPresetPeakAngle;
        optimizationObjectivePreset->setCurrentText(QStringLiteral("Custom"));
        if (optimizationResetObjectiveDefaults != nullptr)
            optimizationResetObjectiveDefaults->click();
        application.processEvents();
    }
    if (optimizationFrontToBackWeight != nullptr)
        optimizationFrontToBackWeight->setValue(1.0);
    auto directionDialogStructured = false;
    QTimer::singleShot(0, &optimization, [&directionDialogStructured] {
        auto* dialog = topLevelDialog(QStringLiteral("optimizationDirectionStrategyDialog"));
        if (dialog == nullptr) return;
        auto* pattern = dialog->findChild<QComboBox*>(
            QStringLiteral("directionSetupPattern"));
        auto* sliceAngle = dialog->findChild<QDoubleSpinBox*>(
            QStringLiteral("directionSetupSliceAngle"));
        auto* measurement = dialog->findChild<QComboBox*>(
            QStringLiteral("directionSetupMeasurement"));
        auto* gainAngle = dialog->findChild<QDoubleSpinBox*>(
            QStringLiteral("directionSetupGainAngle"));
        directionDialogStructured = pattern != nullptr && pattern->count() == 2
            && sliceAngle != nullptr && sliceAngle->value() == 35.0
            && measurement != nullptr && measurement->count() == 2
            && gainAngle != nullptr;
        dialog->reject();
    });
    if (optimizationChooseDirection != nullptr) optimizationChooseDirection->click();
    auto manualDirectionPreserved = false;
    QTimer::singleShot(0, &optimization, [&manualDirectionPreserved] {
        auto* dialog = topLevelDialog(QStringLiteral("optimizationDirectionStrategyDialog"));
        if (dialog == nullptr) return;
        auto* pattern = dialog->findChild<QComboBox*>(
            QStringLiteral("directionSetupPattern"));
        auto* sliceAngle = dialog->findChild<QDoubleSpinBox*>(
            QStringLiteral("directionSetupSliceAngle"));
        auto* measurement = dialog->findChild<QComboBox*>(
            QStringLiteral("directionSetupMeasurement"));
        if (pattern != nullptr) pattern->setCurrentIndex(0);
        if (sliceAngle != nullptr) sliceAngle->setValue(20.0);
        if (measurement != nullptr) measurement->setCurrentIndex(0);
        manualDirectionPreserved = pattern != nullptr && sliceAngle != nullptr
            && measurement != nullptr;
        dialog->accept();
    });
    if (optimizationChooseDirection != nullptr) optimizationChooseDirection->click();
    optimization.setLatestAnalysisResult(latestPattern);
    manualDirectionPreserved = manualDirectionPreserved
        && optimizationDirectionSetupSummary != nullptr
        && optimizationDirectionSetupSummary->text().contains(
            QStringLiteral("Azimuth pattern · strongest gain · elevation 20.000°"));
    QTimer::singleShot(0, &optimization, [] {
        auto* dialog = topLevelDialog(QStringLiteral("optimizationDirectionStrategyDialog"));
        if (dialog == nullptr) return;
        auto* sliceAngle = dialog->findChild<QDoubleSpinBox*>(
            QStringLiteral("directionSetupSliceAngle"));
        if (sliceAngle != nullptr) sliceAngle->setValue(0.0);
        dialog->accept();
    });
    if (optimizationChooseDirection != nullptr) optimizationChooseDirection->click();
    application.processEvents();
    const auto nullCutWarningVisible = optimizationLatestPatternPeakStatus != nullptr
        && optimizationLatestPatternPeakStatus->text().contains(
            QStringLiteral("contains only pattern nulls"));
    if (optimizationUseLatestPatternPeak != nullptr)
        optimizationUseLatestPatternPeak->click();
    const auto latestPatternPeakApplied = optimizationUseLatestPatternPeak != nullptr
        && optimizationUseLatestPatternPeak->isEnabled()
        && optimizationLatestPatternPeakStatus != nullptr
        && optimizationLatestPatternPeakStatus->text().contains(QStringLiteral("@ El 35°"))
        && optimizationObjectiveSummary != nullptr
        && optimizationObjectiveSummary->text().contains(
            QStringLiteral("Track peak · Azimuth cut at El 35° (NEC θ 55°)"))
        && optimizationDirectionSetupSummary != nullptr
        && optimizationDirectionSetupSummary->text().contains(
            QStringLiteral("elevation 35.000°"));
    // A new pattern must replace the cached pattern-peak result.
    auto replacedPattern = latestPattern;
    replacedPattern.radiation.front().thetaDegrees = 40.0;
    optimization.setLatestAnalysisResult(replacedPattern);
    application.processEvents();
    const auto replacedPatternPeakShown = optimizationLatestPatternPeakStatus != nullptr
        && optimizationLatestPatternPeakStatus->text().contains(
            QStringLiteral("@ El 50°"));
    const auto perCriterionDirectionalObjectiveValid = optimizationDirectionalFrequencyNote != nullptr
        && optimizationDirectionalFrequencyNote->text().contains(
            QStringLiteral("Each criterion independently"))
        && patternRunPrerequisiteVisible && latestPatternPeakAutoSeeded
        && maximumGainActionValid
        && automaticDirectionVisible
        && manualDirectionPreserved
        && nullCutWarningVisible
        && latestPatternPeakApplied && replacedPatternPeakShown && directionDialogStructured;
    if (optimizationFrontToBackWeight != nullptr)
        optimizationFrontToBackWeight->setValue(0.0);
    const auto perCriterionFrequencyPlanValid = optimizationFrequencyMode != nullptr
        && optimizationFrequencyMode->isEnabled()
        && optimizationContinuousFrequencies != nullptr
        && !optimizationContinuousFrequencies->isHidden();
    if (optimizationSearchMethods != nullptr) optimizationSearchMethods->setCurrentIndex(1);
    application.processEvents();
    if (optimizationVariables != nullptr && optimizationVariables->rowCount() > 1)
        optimizationVariables->item(1, 0)->setCheckState(Qt::Checked);
    if (optimizationAdaptiveMaximum != nullptr) optimizationAdaptiveMaximum->setValue(35);
    if (optimizationAdaptiveScoreTolerance != nullptr)
        optimizationAdaptiveScoreTolerance->setValue(3.0);
    if (optimizationResetSearchDefaults != nullptr) optimizationResetSearchDefaults->click();
    const auto adaptiveGridAligned = optimizationVariables != nullptr
        && optimizationVariables->isVisible()
        && optimizationAdaptiveMaximum != nullptr && optimizationAdaptiveMaximum->isVisible()
        && optimizationAdaptiveScoreTolerance != nullptr
        && optimizationAdaptiveScoreTolerance->isVisible()
        && optimizationSearchBudgetLabel != nullptr
        && optimizationSearchBudgetLabel->alignment()
            == (Qt::AlignRight | Qt::AlignVCenter);
    const auto adaptiveControlsValid = optimizationSearchMethods != nullptr
        && optimizationSearchMethods->count() == 4
        && optimizationSearchMethods->itemText(0) == QStringLiteral("Parameter Sweep")
        && optimizationSearchMethods->itemText(1) == QStringLiteral("Adaptive Optimize")
        && optimizationSearchMethods->itemText(2) == QStringLiteral("Nelder-Mead")
        && optimizationSearchMethods->itemText(3) == QStringLiteral("Differential Evolution")
        && optimizationScoreToleranceLabel != nullptr
        && optimizationScoreToleranceLabel->isVisible()
        && optimizationAdaptiveMaximum != nullptr && optimizationAdaptiveMaximum->value() == 21
        && optimizationAdaptiveScoreTolerance != nullptr
        && optimizationAdaptiveScoreTolerance->value() == 0.001
        && optimizationWorkload != nullptr
        && optimizationWorkload->text().contains(QStringLiteral("Up to 21 candidates"))
        && optimizationStudySummary != nullptr
        && optimizationStudySummary->text().contains(QStringLiteral("2 parameter(s)"))
        && adaptiveGridAligned;
    if (optimizationSearchMethods != nullptr) optimizationSearchMethods->setCurrentIndex(2);
    application.processEvents();
    const auto nelderMeadControlsValid = optimizationSearchMethods != nullptr
        && !optimizationSearchMethods->itemData(2, Qt::ToolTipRole).toString().isEmpty()
        && optimizationVariables != nullptr && optimizationVariables->isVisible()
        && optimizationAdaptiveMaximum != nullptr && optimizationAdaptiveMaximum->isVisible()
        && optimizationAdaptiveScoreTolerance != nullptr
        && optimizationAdaptiveScoreTolerance->isVisible()
        && optimizationWorkload != nullptr
        && optimizationWorkload->text().contains(QStringLiteral("Up to 21 candidates"));
    if (optimizationSearchMethods != nullptr) optimizationSearchMethods->setCurrentIndex(3);
    application.processEvents();
    const auto differentialEvolutionControlsValid = optimizationSearchMethods != nullptr
        && !optimizationSearchMethods->itemData(3, Qt::ToolTipRole).toString().isEmpty()
        && optimizationVariables != nullptr && optimizationVariables->isVisible()
        && optimizationDifferentialEvolutionSettings != nullptr
        && optimizationDifferentialEvolutionSettings->isVisible()
        && optimizationAdaptiveMaximum != nullptr && !optimizationAdaptiveMaximum->isVisible()
        && optimizationAdaptiveScoreTolerance != nullptr
        && !optimizationAdaptiveScoreTolerance->isVisible()
        && optimizationDifferentialEvolutionPopulation != nullptr
        && optimizationDifferentialEvolutionPopulation->value() == 12
        && optimizationDifferentialEvolutionGenerations != nullptr
        && optimizationDifferentialEvolutionGenerations->value() == 20
        && optimizationDifferentialEvolutionMutation != nullptr
        && optimizationDifferentialEvolutionMutation->value() == 0.8
        && optimizationDifferentialEvolutionCrossover != nullptr
        && optimizationDifferentialEvolutionCrossover->value() == 0.9
        && optimizationDifferentialEvolutionScoreTolerance != nullptr
        && optimizationDifferentialEvolutionScoreTolerance->value() == 0.001
        && optimizationDifferentialEvolutionSeed != nullptr
        && optimizationDifferentialEvolutionSeed->value() == 5489
        && optimizationWorkload != nullptr
        && optimizationWorkload->text().contains(QStringLiteral("Up to 252 candidates"));
    if (optimizationSearchMethods != nullptr) optimizationSearchMethods->setCurrentIndex(0);
    if (optimizationCandidateCount != nullptr) optimizationCandidateCount->setValue(12);
    if (optimizationResetSearchDefaults != nullptr) optimizationResetSearchDefaults->click();
    application.processEvents();
    const auto sweepControlsValid = optimizationScoreToleranceLabel != nullptr
        && !optimizationScoreToleranceLabel->isVisible()
        && optimizationCandidateCount != nullptr
        && optimizationCandidateCount->value() == 7;
    necwb::ui::OptimizationWorkspace sweepSelectionWorkspace;
    sweepSelectionWorkspace.setContext(optimizationSource,
        QStringLiteral("symbol-units.nec"), QStringLiteral("nec2"), {}, 120, true);
    application.processEvents();
    auto* sweepSelectionMethod = sweepSelectionWorkspace.findChild<QComboBox*>(
        QStringLiteral("optimizationSearchMethod"));
    auto* sweepSelectionVariables = sweepSelectionWorkspace.findChild<QTableWidget*>(
        QStringLiteral("optimizationVariablesTable"));
    auto* sweepSelectionRun = sweepSelectionWorkspace.findChild<QPushButton*>(
        QStringLiteral("optimizationRun"));
    auto* sweepSelectionSummary = sweepSelectionWorkspace.findChild<QLabel*>(
        QStringLiteral("optimizationStudySummary"));
    auto sweepSelectionValid = sweepSelectionMethod != nullptr
        && sweepSelectionVariables != nullptr && sweepSelectionRun != nullptr
        && sweepSelectionSummary != nullptr
        && sweepSelectionVariables->rowCount() == 3
        && !sweepSelectionVariables->isHidden()
        && sweepSelectionMethod->currentText() == QStringLiteral("Parameter Sweep")
        && sweepSelectionSummary->text().contains(
            QStringLiteral("Goal: Minimize Worst-Case SWR"));
    if (sweepSelectionValid) {
        sweepSelectionMethod->setCurrentIndex(1);
        sweepSelectionVariables->item(1, 0)->setCheckState(Qt::Checked);
        sweepSelectionMethod->setCurrentIndex(0);
        application.processEvents();
        sweepSelectionValid = sweepSelectionVariables->item(0, 0)->checkState() == Qt::Checked
            && sweepSelectionVariables->item(1, 0)->checkState() == Qt::Checked
            && !sweepSelectionRun->isEnabled()
            && sweepSelectionRun->toolTip().contains(QStringLiteral("Check exactly one"));
        sweepSelectionVariables->item(2, 0)->setCheckState(Qt::Checked);
        application.processEvents();
        sweepSelectionValid = sweepSelectionValid
            && sweepSelectionVariables->item(0, 0)->checkState() == Qt::Unchecked
            && sweepSelectionVariables->item(1, 0)->checkState() == Qt::Unchecked
            && sweepSelectionVariables->item(2, 0)->checkState() == Qt::Checked
            && !sweepSelectionRun->toolTip().contains(QStringLiteral("Check exactly one"))
            && sweepSelectionSummary->text().contains(QStringLiteral("HALF"));
    }
    // Compact sidebar: short table headers, Method before the table in tab order, and a
    // visible gap between the search budget label and its spin box.
    auto compactSearchSectionValid = false;
    if (sweepSelectionValid) {
        sweepSelectionWorkspace.resize(900, 600);
        sweepSelectionWorkspace.show();
        application.processEvents();
        auto* budgetLabel = sweepSelectionWorkspace.findChild<QLabel*>(
            QStringLiteral("optimizationSearchBudgetLabel"));
        auto* candidateCount = sweepSelectionWorkspace.findChild<QSpinBox*>(
            QStringLiteral("optimizationCandidateCount"));
        auto* focus = sweepSelectionMethod->nextInFocusChain();
        while (focus != sweepSelectionVariables && focus != candidateCount
            && focus != sweepSelectionMethod)
            focus = focus->nextInFocusChain();
        compactSearchSectionValid = focus == sweepSelectionVariables
            && sweepSelectionVariables->horizontalHeaderItem(3)->text() == QStringLiteral("Min")
            && sweepSelectionVariables->horizontalHeaderItem(5)->text() == QStringLiteral("Tol.")
            && budgetLabel != nullptr && candidateCount != nullptr
            && candidateCount->geometry().left() - budgetLabel->geometry().right() > 4;
        sweepSelectionWorkspace.hide();
    }
    // Applying a preset silences the objective controls while restoring them; afterwards an
    // edit must still mark the objective Custom.
    auto presetControlsLiveAfterRestore = false;
    if (auto* preset = sweepSelectionWorkspace.findChild<QComboBox*>(
            QStringLiteral("optimizationObjectivePreset"))) {
        auto* swrWeight = sweepSelectionWorkspace.findChild<QDoubleSpinBox*>(
            QStringLiteral("optimizationSwrWeight"));
        preset->setCurrentText(QStringLiteral("Balance SWR and Gain"));
        application.processEvents();
        const auto presetApplied = preset->currentText() == QStringLiteral("Balance SWR and Gain");
        if (swrWeight != nullptr) swrWeight->setValue(swrWeight->value() + 1.0);
        application.processEvents();
        presetControlsLiveAfterRestore = presetApplied && swrWeight != nullptr
            && preset->currentText() == QStringLiteral("Custom");
    }
    auto wheelProtected = false;
    if (optimizationCandidateCount != nullptr) {
        optimizationCandidateCount->clearFocus();
        const auto priorValue = optimizationCandidateCount->value();
        QWheelEvent wheelEvent(QPointF(4, 4), QPointF(4, 4), {}, QPoint(0, 120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(optimizationCandidateCount, &wheelEvent);
        wheelProtected = optimizationCandidateCount->value() == priorValue;
    }
    const auto optimizationDecimalControls = optimization.findChildren<QDoubleSpinBox*>();
    const auto optimizerUsesThreeDecimals = std::ranges::all_of(
        optimizationDecimalControls, [](const auto* control) { return control->decimals() == 3; });
    const auto compactSetupValid = optimizationSetupContent != nullptr
        && optimizationVariableSection != nullptr
        && optimizationFrequencySection != nullptr
        && optimizationObjectiveSection != nullptr
        && optimization.findChild<QTabWidget*>(
            QStringLiteral("optimizationConfigurationTabs")) == nullptr
        && optimization.findChild<QTabWidget*>(
            QStringLiteral("optimizationResultViews")) == nullptr
        && optimizationComparePanel != nullptr
        && optimizationCandidateDetailsWindow != nullptr
        && optimizationCandidateDetailsWindow->isAncestorOf(optimizationComparePanel)
        && optimizationBaselineStatus != nullptr
        && optimizationBaselineStatus->text().contains(
            QStringLiteral("Optimization baseline"))
        && optimizationRun != nullptr
        && optimizationRun->text() == QStringLiteral("Rebuild Baseline && Optimize");
    necwb::ui::OptimizationWorkspace baselineStartWorkspace;
    auto baselineRequested = false;
    baselineStartWorkspace.setBaselineRunCallback(
        [&baselineRequested](const necwb::analysis::FrequencyPlan& plan,
            const necwb::analysis::OptimizationObjectiveSpec& objective) {
            baselineRequested = necwb::analysis::frequencyPlanPoints(plan).size() == 1
                && objective.swrWeight > 0.0;
            return true;
        });
    baselineStartWorkspace.setContext(optimizationSource,
        QStringLiteral("baseline-start.nec"), QStringLiteral("nec2"),
        QCoreApplication::applicationFilePath(), 120, true);
    auto* baselineStartButton = baselineStartWorkspace.findChild<QPushButton*>(
        QStringLiteral("optimizationRun"));
    const auto baselineStartTooltipValid = baselineStartButton != nullptr
        && baselineStartButton->toolTip().contains(
            QStringLiteral("Normal Results are not populated"));
    if (baselineStartButton != nullptr) baselineStartButton->click();
    const auto baselineOrchestrationValid = baselineRequested
        && baselineStartTooltipValid
        && baselineStartButton != nullptr
        && baselineStartButton->text() == QStringLiteral("Building Baseline…");
    baselineStartWorkspace.finishOptimizationBaseline(false,
        QStringLiteral("expected smoke-test stop"));
    if (const auto nec2c = QStandardPaths::findExecutable(QStringLiteral("nec2c")); nec2c.isEmpty()) {
        qWarning() << "nec2c not found; skipping the optimization metadata tolerance check";
    } else {
        // Session and candidate metadata record the tolerance the study ran with, not the
        // Differential Evolution one beside it.
        necwb::ui::OptimizationWorkspace toleranceWorkspace;
        toleranceWorkspace.setContext(optimizationSource, QStringLiteral("tolerance.nec"),
            QStringLiteral("nec2"), nec2c, 60, true);
        necwb::analysis::AnalysisResult toleranceBaseline;
        toleranceBaseline.feedpoints = {{.frequencyMHz = 7.15, .wireTag = 1, .segment = 11,
            .voltage = {1.0, 0.0}, .current = {0.02, 0.0}, .impedance = {50.0, 0.0}}};
        necwb::ui::AnalysisRunRecord toleranceRecord;
        toleranceRecord.id = QStringLiteral("tolerance-baseline");
        toleranceRecord.backend = QStringLiteral("nec2");
        toleranceRecord.referenceImpedanceOhms = 50.0;
        toleranceRecord.impedanceFrequenciesMHz = {7.15};
        toleranceRecord.modelFingerprint = QString::fromStdString(
            necwb::analysis::optimizationModelFingerprint(necwb::nec::NecSymbolResolver{}
                .resolve(optimizationSource.toStdString()).generatedDeck));
        toleranceWorkspace.setOptimizationBaseline(toleranceRecord, toleranceBaseline);
        auto* method = toleranceWorkspace.findChild<QComboBox*>(
            QStringLiteral("optimizationSearchMethod"));
        auto* adaptiveTolerance = toleranceWorkspace.findChild<QDoubleSpinBox*>(
            QStringLiteral("optimizationAdaptiveScoreTolerance"));
        auto* evolutionTolerance = toleranceWorkspace.findChild<QDoubleSpinBox*>(
            QStringLiteral("optimizationDifferentialEvolutionScoreTolerance"));
        auto* run = toleranceWorkspace.findChild<QPushButton*>(QStringLiteral("optimizationRun"));
        if (method == nullptr || adaptiveTolerance == nullptr || evolutionTolerance == nullptr
            || run == nullptr)
            return NECWB_SMOKE_FAILURE("optimization tolerance controls");
        method->setCurrentIndex(method->findData(1));
        adaptiveTolerance->setValue(0.25);
        evolutionTolerance->setValue(0.75);
        necwb::ui::AnalysisRunStore toleranceStore;
        const auto before = toleranceStore.load().size();
        run->click();
        const auto metadataOf = [&](const QString& runType, const QString& file) {
            for (const auto& record : toleranceStore.load()) {
                if (record.runType != runType || record.sourceFile != QStringLiteral("tolerance.nec"))
                    continue;
                QFile json(QDir(record.directory).filePath(file));
                if (json.open(QIODevice::ReadOnly)) return QJsonDocument::fromJson(json.readAll()).object();
            }
            return QJsonObject{};
        };
        QElapsedTimer waited;
        waited.start();
        while (metadataOf(QStringLiteral("optimization-candidate"),
                   QStringLiteral("optimization.json")).isEmpty() && waited.elapsed() < 20000)
            application.processEvents(QEventLoop::AllEvents, 50);
        toleranceWorkspace.cancelAndWait();
        const auto session = metadataOf(QStringLiteral("optimization-session"),
            QStringLiteral("optimization-session.json"));
        const auto candidate = metadataOf(QStringLiteral("optimization-candidate"),
            QStringLiteral("optimization.json"));
        const auto toleranceRecorded = toleranceStore.load().size() > before
            && session.value(QStringLiteral("searchMethod")).toString() == QStringLiteral("adaptive")
            && session.value(QStringLiteral("scoreTolerance")).toDouble() == 0.25
            && candidate.value(QStringLiteral("searchMethod")).toString() == QStringLiteral("adaptive")
            && candidate.value(QStringLiteral("scoreTolerance")).toDouble() == 0.25
            && candidate.value(QStringLiteral("swrWeight")) == session.value(QStringLiteral("swrWeight"))
            && candidate.value(QStringLiteral("frequenciesMHz")) == session.value(QStringLiteral("frequenciesMHz"));
        for (const auto& record : toleranceStore.load())
            if (record.sourceFile == QStringLiteral("tolerance.nec") && record.parentId.isEmpty())
                toleranceStore.removeGroup(record.id, record.directory);
        if (!toleranceRecorded) {
            qWarning() << "optimization tolerance" << session << candidate;
            return NECWB_SMOKE_FAILURE("optimization metadata tolerance");
        }
        // A study runs on the model as it was when it started; applying a candidate after the
        // model changed asks first, listing what differs, while the applied values themselves
        // are not a change.
        necwb::ui::OptimizationWorkspace changedWorkspace;
        changedWorkspace.setContext(optimizationSource, QStringLiteral("changed.nec"),
            QStringLiteral("nec2"), nec2c, 60, true);
        changedWorkspace.setOptimizationBaseline(toleranceRecord, toleranceBaseline);
        auto applied = 0;
        changedWorkspace.setApplyParameterCallback(
            [&applied](const std::vector<std::pair<QString, double>>&) { return ++applied, true; });
        auto* points = changedWorkspace.findChild<QSpinBox*>(
            QStringLiteral("optimizationCandidateCount"));
        auto* changedRun = changedWorkspace.findChild<QPushButton*>(QStringLiteral("optimizationRun"));
        auto* applyBest = changedWorkspace.findChild<QPushButton*>(
            QStringLiteral("optimizationApplyBest"));
        if (points == nullptr || changedRun == nullptr || applyBest == nullptr)
            return NECWB_SMOKE_FAILURE("optimization apply-after-change controls");
        points->setValue(points->minimum());
        changedRun->click();
        const auto runningAtEdit = changedWorkspace.isRunning();
        auto edited = optimizationSource;
        edited.replace(QStringLiteral("0.001"), QStringLiteral("0.002"));
        changedWorkspace.setContext(edited, QStringLiteral("changed.nec"),
            QStringLiteral("nec2"), nec2c, 60, true);
        QElapsedTimer changedWait;
        changedWait.start();
        while (changedWorkspace.isRunning() && changedWait.elapsed() < 30000)
            application.processEvents(QEventLoop::AllEvents, 50);
        QString promptText;
        const auto answer = [&promptText](QMessageBox::StandardButton button) {
            QTimer::singleShot(0, [&promptText, button] {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    promptText = box->text() + QLatin1Char('\n') + box->informativeText();
                    box->button(button)->click();
                }
            });
        };
        answer(QMessageBox::No);
        applyBest->click();
        const auto declined = applied == 0
            && promptText.startsWith(QStringLiteral("The model changed since this optimization started"))
            && promptText.contains(QStringLiteral("GW (line 4), field 9: 0.001 → 0.002"));
        answer(QMessageBox::Yes);
        applyBest->click();
        const auto appliedAnyway = applied == 1;
        // Back to the model the study ran on, with a different optimized value: no question.
        promptText.clear();
        auto revalued = optimizationSource;
        revalued.replace(QStringLiteral("LONG_FT=95"), QStringLiteral("LONG_FT=97"));
        changedWorkspace.setContext(revalued, QStringLiteral("changed.nec"),
            QStringLiteral("nec2"), nec2c, 60, true);
        applyBest->click();
        application.processEvents();
        const auto unchangedApplies = applied == 2 && promptText.isEmpty();
        for (const auto& record : toleranceStore.load())
            if (record.sourceFile == QStringLiteral("changed.nec") && record.parentId.isEmpty())
                toleranceStore.removeGroup(record.id, record.directory);
        if (!runningAtEdit || !declined || !appliedAnyway || !unchangedApplies) {
            qWarning() << "apply after change" << runningAtEdit << declined << appliedAnyway
                       << unchangedApplies << applied << promptText;
            return NECWB_SMOKE_FAILURE("optimization asks before applying to a changed model");
        }
    }
    necwb::ui::OptimizationWorkspace directionalBaselineWorkspace;
    directionalBaselineWorkspace.setContext(optimizationSource,
        QStringLiteral("directional-baseline.nec"), QStringLiteral("nec2"), {}, 120, true);
    directionalBaselineWorkspace.setLatestAnalysisResult(latestPattern);
    auto* directionalBaselinePreset = directionalBaselineWorkspace.findChild<QComboBox*>(
        QStringLiteral("optimizationObjectivePreset"));
    if (directionalBaselinePreset != nullptr) {
        directionalBaselinePreset->setCurrentText(
            QStringLiteral("Maximize Peak Gain in Cut"));
    }
    const auto directionalResolution = necwb::nec::NecSymbolResolver{}.resolve(
        optimizationSource.toStdString());
    necwb::analysis::AnalysisResult mismatchedDirectionalBaseline;
    mismatchedDirectionalBaseline.radiation = {
        {.frequencyMHz = 7.15, .thetaDegrees = 0.0, .phiDegrees = 0.0,
            .totalGainDb = 1.0, .patternIndex = 0},
        {.frequencyMHz = 7.15, .thetaDegrees = 5.0, .phiDegrees = 0.0,
            .totalGainDb = 1.0, .patternIndex = 0},
    };
    necwb::ui::AnalysisRunRecord mismatchedDirectionalRecord;
    mismatchedDirectionalRecord.id = QStringLiteral("mismatched-directional-grid");
    mismatchedDirectionalRecord.backend = QStringLiteral("nec2");
    mismatchedDirectionalRecord.modelFingerprint = QString::fromStdString(
        necwb::analysis::optimizationModelFingerprint(
            directionalResolution.generatedDeck));
    mismatchedDirectionalRecord.radiationCoverage =
        necwb::analysis::optimizationResultCoverage(
            mismatchedDirectionalBaseline).radiation;
    directionalBaselineWorkspace.setOptimizationBaseline(
        mismatchedDirectionalRecord, mismatchedDirectionalBaseline);
    application.processEvents();
    auto mismatchedDirectionalGridReported = false;
    for (auto* window : QApplication::topLevelWidgets()) {
        if (window->objectName() != QStringLiteral("optimizationCandidateDetailsWindow"))
            continue;
        const auto* status = window->findChild<QLabel*>(
            QStringLiteral("optimizationBaselineStatus"));
        if (status != nullptr && status->text().contains(
                QStringLiteral("directional cut grid differs"))) {
            mismatchedDirectionalGridReported = true;
            break;
        }
    }
    necwb::ui::OptimizationWorkspace staleDirectionWorkspace;
    staleDirectionWorkspace.setContext(optimizationSource,
        QStringLiteral("stale-direction.nec"), QStringLiteral("nec2"), {}, 120, true);
    staleDirectionWorkspace.setFindMaximumGainCallback([](double) { return false; });
    staleDirectionWorkspace.setLatestAnalysisResult(latestPattern);
    staleDirectionWorkspace.setContext(QStringLiteral("CM edited\n") + optimizationSource,
        QStringLiteral("stale-direction.nec"), QStringLiteral("nec2"), {}, 120, true);
    auto* staleDirectionStatus = staleDirectionWorkspace.findChild<QLabel*>(
        QStringLiteral("optimizationLatestPatternPeakStatus"));
    auto* staleDirectionButton = staleDirectionWorkspace.findChild<QPushButton*>(
        QStringLiteral("optimizationFindMaximumGain"));
    const auto staleDirectionReported = staleDirectionStatus != nullptr
        && staleDirectionStatus->text().contains(QStringLiteral("direction is stale"))
        && staleDirectionButton != nullptr
        && staleDirectionButton->text() == QStringLiteral("Update Sampled Peak…");
    necwb::ui::OptimizationComparePanel directionalComparison;
    necwb::analysis::AnalysisResult directionalCandidateResult;
    necwb::analysis::AnalysisResult directionalBaselineResult;
    necwb::analysis::OptimizationObjectiveResult directionalCandidateEvaluation;
    directionalCandidateEvaluation.frequencyMetrics = {{
        .frequencyMHz = 14.0,
        .forwardGainDb = 7.0,
        .frontToBackDb = 18.0,
        .frontToRearDb = 15.0,
    }};
    auto directionalBaselineEvaluation = directionalCandidateEvaluation;
    directionalBaselineEvaluation.frequencyMetrics.front().forwardGainDb = 6.5;
    directionalComparison.setBaseline(directionalBaselineResult,
        QStringLiteral("Starting model"));
    directionalComparison.setBaselineStatus(QStringLiteral("Compatible"), true);
    directionalComparison.setCandidate(directionalCandidateResult,
        QStringLiteral("Candidate"), {}, {}, directionalCandidateEvaluation,
        directionalBaselineEvaluation);
    auto* directionalComparisonTabs = directionalComparison.findChild<QTabWidget*>(
        QStringLiteral("optimizationCompareTabs"));
    auto* directionalComparisonControl = directionalComparison.findChild<QCheckBox*>(
        QStringLiteral("optimizationCompareWithStartingModel"));
    auto* directionalComparisonPlot = directionalComparison.findChild<QWidget*>(
        QStringLiteral("directionalMetricsPlot"));
    const auto directionalCandidateOnlyVisible = directionalComparisonTabs != nullptr
        && directionalComparisonTabs->isTabVisible(2)
        && directionalComparisonTabs->tabText(2)
            == QStringLiteral("Directional vs Frequency")
        && directionalComparisonPlot != nullptr
        && directionalComparisonPlot->property("seriesCount").toInt() == 3;
    if (directionalComparisonControl != nullptr) directionalComparisonControl->click();
    application.processEvents();
    const auto directionalStartingModelOverlay = directionalCandidateOnlyVisible
        && directionalComparisonControl->isChecked()
        && directionalComparisonPlot->property("seriesCount").toInt() == 6;
    if (!hiddenCandidateDetailsRestored)
        return NECWB_SMOKE_FAILURE("hidden candidate review restore");
    if (!minimizedCandidateDetailsRestored)
        return NECWB_SMOKE_FAILURE("minimized candidate review restore");
    if (!candidateReviewGeometryPersisted)
        return NECWB_SMOKE_FAILURE("candidate review geometry or size persistence");
    if (optimizationCandidateReviewDefaultSize != expectedCandidateReviewSize)
        return NECWB_SMOKE_FAILURE("candidate review responsive default size");
    auto* optimizationFrontToRearRegion = optimization.findChild<QComboBox*>(
        QStringLiteral("optimizationFrontToRearRegion"));
    auto frontToRearRegionSelectable = false;
    if (optimizationFrontToRearRegion != nullptr && optimizationObjectiveCriteria != nullptr) {
        const auto defaultLabel = optimizationObjectiveCriteria->item(5, 0)->text();
        const auto previousRegion = optimizationFrontToRearRegion->currentIndex();
        optimizationFrontToRearRegion->setCurrentIndex(optimizationFrontToRearRegion->findData(
            static_cast<int>(necwb::analysis::FrontToRearRegion::RearHemisphere3D)));
        application.processEvents();
        frontToRearRegionSelectable = optimizationFrontToRearRegion->count() == 2
            && defaultLabel == QStringLiteral("F/R (dB)")
            && optimizationObjectiveCriteria->item(5, 0)->text()
                == QStringLiteral("F/R (3D rear hemisphere) (dB)")
            && optimizationObjectiveCriteria->item(5, 0)->toolTip().contains(
                QStringLiteral("any elevation"));
        optimizationFrontToRearRegion->setCurrentIndex(previousRegion);
        application.processEvents();
    }
    if (optimizationConfiguration == nullptr
        || optimizationWorkspaceSplitter == nullptr
        || optimizationWorkspaceSplitter->orientation() != Qt::Horizontal
        || optimizationWorkspaceSplitter->count() != 2
        || optimizationWorkspaceSplitter->childrenCollapsible()
        || optimizationResultsPanel == nullptr
        || optimizationConfigurationScrollArea == nullptr
        || optimizationConfigurationScrollArea->verticalScrollBarPolicy()
            != Qt::ScrollBarAsNeeded
        || optimizationConfigurationScrollArea->horizontalScrollBarPolicy()
            != Qt::ScrollBarAlwaysOff
        || optimizationResultsTable == nullptr
        || optimizationResultsTable->contextMenuPolicy() != Qt::CustomContextMenu
        || !optimizationResultsTable->toolTip().contains(QStringLiteral("Right-click"))
        || optimizationCandidateHint == nullptr
        || !optimizationCandidateHint->text().contains(
            QStringLiteral("right-click"), Qt::CaseInsensitive)
        || optimizationCandidatePlots == nullptr
        || optimizationCandidateDetailsWindow == nullptr
        || optimizationCandidateDetailsWindow->parentWidget() != nullptr
        || optimizationCandidateDetailsWindow->testAttribute(Qt::WA_QuitOnClose)
        || optimizationCandidateDetailsWindow->isVisible()
        || optimizationCandidateDetailsWindow->windowTitle() != QStringLiteral("Candidate Results")
        || optimizationMargins.left() > 4 || optimizationMargins.top() > 4
        || optimizationMargins.right() > 4 || optimizationMargins.bottom() > 4
        || candidateReviewMargins.left() > 4 || candidateReviewMargins.top() > 4
        || candidateReviewMargins.right() > 4 || candidateReviewMargins.bottom() > 4
        || !staleDirectionReported
        || !directionalStartingModelOverlay
        || optimizationCandidateDetails == nullptr
        || optimizationCandidateDetails->columnCount() != 9
        || optimizationCandidateDetails->verticalHeader()->defaultSectionSize() > 24
        || optimizationResultsTable->verticalHeader()->defaultSectionSize() > 22
        || optimizationDirectionSetupSummary == nullptr
        || optimizationChooseDirection == nullptr
        || optimizationFindMaximumGain == nullptr
        || !optimizationCandidateDetailsWindow->isAncestorOf(
            optimizationCandidateDetails)
        || optimizationCandidateDetailViews == nullptr
        || optimizationCandidateDetailViews->count() != 2
        || optimizationCandidateDetailViews->tabText(0)
            != QStringLiteral("Results")
        || optimizationCandidateDetailViews->tabText(1)
            != QStringLiteral("Frequency Table")
        || optimizationCandidateDetailsWindow->findChild<QWidget*>(
            QStringLiteral("optimizationCandidateDetailPlots")) != nullptr
        || optimizationCandidateDirectionalPlots == nullptr
        || !optimizationCandidateDetailsWindow->isAncestorOf(
            optimizationCandidateDirectionalPlots)
        || optimizationApplyCandidate == nullptr || optimizationApplyCandidate->isEnabled()
        || optimizationApplyCandidateAndRun == nullptr
        || optimizationApplyCandidateAndRun->isEnabled()
        || optimizationApplyCandidateAndRun->toolTip().isEmpty()
        || optimizationCompareTabs == nullptr || optimizationCompareTabs->count() != 4
        || optimizationCompareWithStartingModel == nullptr
        || !candidateComparisonDefaultsOff
        || optimizationCompareTabs->tabText(0) != QStringLiteral("SWR && Impedance")
        || optimizationCompareTabs->tabText(1) != QStringLiteral("2D Pattern")
        || optimizationCompareTabs->tabText(2)
            != QStringLiteral("Directional vs Frequency")
        || optimizationCompareTabs->isTabVisible(2)
        || optimizationCompareTabs->tabText(3) != QStringLiteral("Objective Details")
        || optimizationResultsSplitter == nullptr
        || optimizationResultsSplitter->orientation() != Qt::Vertical
        || optimizationResultsSplitter->count() != 2
        || optimizationResultsSplitter->childrenCollapsible()
        || !optimizationResultsSplitter->isAncestorOf(optimizationResultsTable)
        || !optimizationResultsSplitter->isAncestorOf(optimizationCandidatePlots)
        || optimizationVariables == nullptr
        || optimizationSearchBudgetLabel == nullptr
        || !differentialEvolutionControlsValid
        || optimizationParameterSettings == nullptr
        || optimizationObjectiveSettings == nullptr
        || !objectivePresetsValid
        || !directionalPresetReadinessValid
        || !directionalPresetAutoSeeded
        || !frontToRearRegionSelectable
        || optimizationObjectiveCriteria == nullptr
        || optimizationSwrWeight == nullptr || optimizationSwrWeight->value() != 1.0
        || optimizationResistanceWeight == nullptr || optimizationResistanceWeight->value() != 0.0
        || optimizationResistanceTarget == nullptr || optimizationResistanceTarget->value() != 50.0
        || optimizationReactanceWeight == nullptr || optimizationReactanceWeight->value() != 0.0
        || optimizationReactanceTarget == nullptr || optimizationReactanceTarget->value() != 0.0
        || optimizationStudySummary == nullptr
        || !optimizationStudySummary->text().contains(QStringLiteral("LONG_FT"))
        || !optimizationStudySummary->text().contains(QStringLiteral("Frequencies:"))
        || optimizationFrequencyMode == nullptr
        || optimizationFrequencySummary == nullptr
        || optimizationFrequencySummary->text().isEmpty()
        || optimizationEditFrequencies == nullptr
        || optimizationFrequencyDialog == nullptr
        || optimizationObjectiveSummary == nullptr
        || optimizationObjectiveSummary->text().isEmpty()
        || optimizationEditObjective == nullptr
        || optimizationObjectiveDialog == nullptr
        || optimizationSwrGoal == nullptr || optimizationSwrGoal->count() != 5
        || optimizationSwrGoal->currentText() != QStringLiteral("Minimize")
        || optimizationGainGoal == nullptr
        || optimizationGainGoal->currentText() != QStringLiteral("Maximize")
        || optimizationSwrAggregation == nullptr
        || optimizationSwrAggregation->currentText() != QStringLiteral("Worst Point")
        || optimizationGainAggregation == nullptr
        || optimizationGainAggregation->currentText() != QStringLiteral("Worst Point")
        || optimizationSwrValue == nullptr || optimizationSwrValue->value() != 2.0
        || optimizationResetObjectiveDefaults == nullptr
        || optimizationObjectiveExplanation == nullptr
        || !goalAwareBandEvaluationValid
        || optimizationFrontToRearWeight == nullptr
        || optimizationFrequencyTable == nullptr
        || optimizationAddAmateurBand == nullptr
        || optimizationClearFrequencies == nullptr
        || optimizationExplicitFrequencies == nullptr
        || optimizationContinuousStart == nullptr
        || optimizationContinuousStop == nullptr
        || optimizationContinuousStep == nullptr
        || optimizationApplyBest == nullptr || optimizationApplyBest->isEnabled()
        || optimizationResetSearchDefaults == nullptr
        || optimizationCandidateCount == nullptr
        || !optimizationCandidateCount->toolTip().contains(
            QStringLiteral("including both endpoints"))
        || !optimizationCandidateCount->toolTip().contains(
            QStringLiteral("every selected frequency"))
        || !adaptiveControlsValid
        || !nelderMeadControlsValid
        || !sweepControlsValid
        || !sweepSelectionValid
        || !compactSearchSectionValid
        || !presetControlsLiveAfterRestore
        || !legacySessionLoaded
        || !wheelProtected
        || !compactSetupValid
        || !baselineOrchestrationValid
        || !mismatchedDirectionalGridReported
        || !frequencyControlsStructured
        || !hasDeleteShortcut
        || !hasBackspaceShortcut
        || !continuousSweepValid
        || !perCriterionFrequencyPlanValid
        || optimizationFrequencyTable->viewMode() != QListView::IconMode
        || !optimizationFrequencyTable->isWrapping()
        || optimizationFrequencyTable->count() != 7
        || !optimizerUsesThreeDecimals
        || !optimizerResultsPreserved
        || !sameModelFrequenciesPreserved
        || !historicalFrequencySelectionLoaded
        || !historicalCandidateResultCached
        || !historicalSetupHidden
        || !historicalObjectiveBreakdownLoaded
        || !historicalBaselineLoaded
        || !historicalBaselineDeltaLoaded
        || !numericCandidateSortingEnabled
        || !informationalCandidateColumnsStayUnsorted
        || !historicalInputInitiallyHidden || !authoredInputShown || !generatedInputShown
        || !activeFrequenciesRestored
        || !candidateApplyPreservesDetails
        || !frequencyCancelRestored
        || !objectiveCancelRestored
        || !perCriterionDirectionalObjectiveValid
        || optimizationObjectiveCriteria->rowCount() != 6
        || optimizationObjectiveCriteria->columnCount() != 5
        || optimizationObjectiveCriteria->horizontalHeaderItem(4)->text()
            != QStringLiteral("Band Evaluation")
        || optimizationResultsTable->columnCount() != 11
        || optimizationVariables->columnCount() != 6
        || optimizationMakeVariable == nullptr
        || !summaryFitsText(optimization.findChild<QLabel*>(
            QStringLiteral("optimizationFrequencySummary")))
        || !parameterizationHelpInvoked
        || optimizationMakeVariable->text() != QStringLiteral("Make Variable…")
        || !optimizationMakeVariable->toolTip().contains(
            QStringLiteral("choose Make Variable…"))
        || optimizationVariables->rowCount() != 3
        || optimizationVariables->item(0, 0)->checkState() != Qt::Checked
        || optimizationVariables->item(1, 0)->checkState() != Qt::Checked
        || optimizationVariables->item(0, 1)->text() != QStringLiteral("LONG_FT")
        || optimizationVariables->item(0, 1)->toolTip().isEmpty()
        || optimizationVariables->item(0, 2)->text() != QStringLiteral("95.000")
        || optimizationVariables->item(0, 3)->text() != QStringLiteral("76.000")
        || optimizationVariables->item(0, 4)->text() != QStringLiteral("114.000")
        || optimizationVariables->item(2, 1)->text() != QStringLiteral("HALF")) {
        return NECWB_SMOKE_FAILURE("optimization workspace");
    }
    NECWB_SMOKE_STEP();
    {
        // Two parallel elements entered in opposite directions with the same physical current:
        // the Antenna tab and the relative-phase column must show them in phase.
        necwb::model::AntennaModel parallelPair;
        parallelPair.addWire({1, {0.0, -1.0, 0.0}, {0.0, 1.0, 0.0}, 2, 0.001, 1});
        parallelPair.addWire({2, {0.5, 1.0, 0.0}, {0.5, -1.0, 0.0}, 2, 0.001, 2});
        necwb::analysis::AnalysisResult pairResult;
        const auto pairCurrent = [](int tag, int segment, double magnitude, double phase) {
            return necwb::analysis::SegmentCurrentResult{.frequencyMHz = 14.0, .segment = segment,
                .wireTag = tag, .current = std::polar(magnitude, phase * std::numbers::pi / 180.0),
                .magnitude = magnitude, .phaseDegrees = phase};
        };
        pairResult.currents = {pairCurrent(1, 1, 1.0, 30.0), pairCurrent(1, 2, 1.0, 30.0),
            pairCurrent(2, 1, 0.5, -150.0), pairCurrent(2, 2, 0.5, -150.0)};
        necwb::analysis::FeedpointResult pairFeed;
        pairFeed.frequencyMHz = 14.0;
        pairFeed.wireTag = 1;
        pairFeed.segment = 1;
        pairFeed.current = std::polar(1.0, 30.0 * std::numbers::pi / 180.0);
        pairResult.feedpoints = {pairFeed};
        necwb::ui::CurrentDistributionView pairView;
        pairView.resize(800, 600);
        pairView.setModel(parallelPair);
        pairView.setResults(necwb::ui::ResultData::make(pairResult), QStringLiteral("pair-run"));
        pairView.setSelectedFrequency(14.0);
        pairView.show();
        application.processEvents();
        auto* views = pairView.findChild<QTabWidget*>(QStringLiteral("currentsViews"));
        auto* canvas = pairView.findChild<QWidget*>(QStringLiteral("currentsAntennaCanvas"));
        auto* mode = pairView.findChild<QComboBox*>(QStringLiteral("currentsDisplayMode"));
        auto* cycle = pairView.findChild<QSlider*>(QStringLiteral("currentsCycle"));
        auto* play = pairView.findChild<QPushButton*>(QStringLiteral("currentsPlay"));
        auto* table = pairView.findChild<QTableWidget*>();
        auto antennaValid = views != nullptr && views->tabText(0) == QStringLiteral("Antenna")
            && canvas != nullptr && canvas->property("segmentCount").toInt() == 4
            && mode != nullptr && cycle != nullptr && play != nullptr && play->isHidden()
            && table != nullptr && table->rowCount() == 4
            && table->horizontalHeaderItem(5)->text() == QStringLiteral("Phase vs. feed (°)");
        for (auto row = 0; antennaValid && row < table->rowCount(); ++row)
            antennaValid = std::abs(table->item(row, 5)->text().toDouble()) < 1.0e-6;
        if (antennaValid) {
            mode->setCurrentIndex(2);
            application.processEvents();
            antennaValid = !play->isHidden() && cycle->value() == 0;
            play->click();
            QElapsedTimer animationTime;
            animationTime.start();
            while (cycle->value() == 0 && animationTime.elapsed() < 2000)
                application.processEvents(QEventLoop::AllEvents, 50);
            antennaValid = antennaValid && cycle->value() > 0 && play->text() == QStringLiteral("Pause");
            play->click();
            const QPointF middle(canvas->width() / 2.0, canvas->height() / 2.0);
            QWheelEvent zoomIn(middle, canvas->mapToGlobal(middle), {}, {0, 240}, Qt::NoButton,
                Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(canvas, &zoomIn);
            antennaValid = antennaValid && canvas->property("zoom").toDouble() > 1.0;
            QMouseEvent fit(QEvent::MouseButtonDblClick, middle, canvas->mapToGlobal(middle),
                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &fit);
            antennaValid = antennaValid && canvas->property("zoom").toDouble() == 1.0;
            // Sweep the cursor across the view until it lands on a wire.
            auto hoverText = QString{};
            for (auto x = 0; x < canvas->width() && hoverText.isEmpty(); x += 2) {
                const QPointF point(x, canvas->height() / 2.0);
                QMouseEvent move(QEvent::MouseMove, point, canvas->mapToGlobal(point),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &move);
                canvas->repaint();
                hoverText = canvas->property("hoverReadout").toString();
            }
            antennaValid = antennaValid && hoverText.contains(QStringLiteral("vs. feed"))
                && hoverText.contains(QStringLiteral("Phase 0.0°"));
        }
        if (!antennaValid) return NECWB_SMOKE_FAILURE("currents antenna view");
    }
    // The 3D pattern is centred on the antenna and sized by it: a three-element beam moved
    // up and sideways draws exactly as it does at the origin, in free space and over ground
    // and in both scales, and the antenna shows in it.
    NECWB_SMOKE_STEP();
    {
        const auto beam = [](necwb::model::Point3D offset) {
            necwb::model::AntennaModel model;
            const std::array<std::pair<double, double>, 3> elements{{{-1.5, 5.4}, {0.0, 5.1}, {1.2, 4.8}}};
            for (auto index = 0; index < 3; ++index) {
                const auto [x, half] = elements[static_cast<std::size_t>(index)];
                model.addWire({index + 1, {x + offset.x, -half + offset.y, offset.z},
                    {x + offset.x, half + offset.y, offset.z}, 11, 0.001, static_cast<std::size_t>(index + 1)});
            }
            return model;
        };
        const necwb::model::Point3D shift{3.0, -2.0, 10.0};
        const auto atOrigin = beam({});
        const auto moved = beam(shift);
        const auto placed = necwb::ui::patternPlacement(atOrigin);
        const auto placedMoved = necwb::ui::patternPlacement(moved);
        const auto close = [](const necwb::model::Point3D& a, const necwb::model::Point3D& b) {
            return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9 && std::abs(a.z - b.z) < 1e-9;
        };
        // Centred on the bounding box, the same size wherever it is, and only shifted and
        // uniformly scaled; with no antenna, at the origin.
        const auto placementFollows = close(placed.centre, {-0.15, 0.0, 0.0})
            && std::abs(placed.halfSize - 5.4) < 1e-9
            && close(placedMoved.centre, {-0.15 + shift.x, shift.y, shift.z})
            && std::abs(placedMoved.halfSize - placed.halfSize) < 1e-9
            && close(placedMoved.toDisplay({1.2 + shift.x, 4.8 + shift.y, shift.z}, 0.32),
                placed.toDisplay({1.2, 4.8, 0.0}, 0.32))
            && close(placed.toDisplay({-0.15, 5.4, 0.0}, 0.32), {0.0, 0.32, 0.0})
            && close(placedMoved.toDisplay({-0.15 + shift.x, shift.y, 0.0}, 0.32),
                {0.0, 0.0, -shift.z * 0.32 / 5.4});
        const auto empty = necwb::ui::patternPlacement(necwb::model::AntennaModel{});
        const auto patternOnly = close(empty.centre, {}) && empty.halfSize == 1.0;

        // A forward lobe, over the whole sphere or only above the horizon.
        const auto pattern = [](bool overGround) {
            necwb::analysis::AnalysisResult result;
            for (auto theta = 0; theta <= (overGround ? 90 : 180); theta += 10)
                for (auto phi = 0; phi < 360; phi += 10) {
                    const auto t = theta * std::numbers::pi / 180.0, f = phi * std::numbers::pi / 180.0;
                    const auto gain = 7.0 + 10.0 * std::log10(std::max(1e-3,
                        (1.0 + std::sin(t) * std::cos(f)) / 2.0 * (overGround ? std::sin(2.0 * t) + 0.05 : 1.0)));
                    result.radiation.push_back({14.2, double(theta), double(phi), gain - 3.0, gain - 3.0, gain});
                }
            return necwb::ui::ResultData::make(result);
        };
        const auto render = [&](const necwb::model::AntennaModel& model, bool overGround,
                                necwb::analysis::RadiationScale scale) {
            necwb::ui::Radiation3DView view;
            view.resize(800, 600);
            view.setModel(model);
            view.setResults(pattern(overGround), QStringLiteral("beam"));
            view.setDisplaySettings({.frequencyMHz = 14.2, .scale = scale});
            view.show();
            application.processEvents();
            auto* surface = view.findChild<QWidget*>(QStringLiteral("radiation3DSurface"));
            return surface != nullptr ? surface->grab().toImage() : QImage{};
        };
        // Pixels that differ by more than a rounding step.
        const auto differing = [](const QImage& a, const QImage& b) {
            if (a.size() != b.size() || a.isNull()) return std::numeric_limits<int>::max();
            auto count = 0;
            for (auto y = 0; y < a.height(); ++y)
                for (auto x = 0; x < a.width(); ++x) {
                    const auto p = a.pixelColor(x, y), q = b.pixelColor(x, y);
                    if (std::abs(p.red() - q.red()) + std::abs(p.green() - q.green())
                            + std::abs(p.blue() - q.blue()) > 24) ++count;
                }
            return count;
        };
        QStringList seen;
        auto followsAntenna = true;
        for (const auto overGround : {false, true})
            for (const auto scale : {necwb::analysis::RadiationScale::Normalized,
                     necwb::analysis::RadiationScale::Absolute}) {
                const auto here = render(atOrigin, overGround, scale);
                const auto there = render(moved, overGround, scale);
                const auto bare = render({}, overGround, scale);
                const auto same = differing(here, there);
                const auto antennaShows = differing(here, bare);
                seen << QStringLiteral("%1/%2: moved %3 px, antenna %4 px").arg(overGround ? "ground" : "free")
                            .arg(static_cast<int>(scale)).arg(same).arg(antennaShows);
                followsAntenna = followsAntenna && same <= 20 && antennaShows > 200;
            }
        if (!placementFollows || !patternOnly || !followsAntenna) {
            qWarning() << "pattern on antenna" << placementFollows << patternOnly << seen;
            return NECWB_SMOKE_FAILURE("3D pattern centred on and sized by the antenna");
        }
    }
    necwb::ui::CurrentDistributionView currents;
    necwb::ui::RadiationPatternView radiation2D;
    necwb::ui::Radiation3DView radiation3D;
    necwb::ui::RadiationPerformanceView radiationPerformance;
    for (auto* resultView : std::vector<QWidget*>{
             &currents, &radiation2D, &radiation3D, &radiationPerformance}) {
        resultView->resize(800, 600);
    }
    necwb::model::AntennaModel currentAntenna;
    currentAntenna.addWire({1, {0.0, 0.0, 0.0}, {5.0, 0.0, 0.0}, 2, 0.001, 1});
    currentAntenna.addWire({2, {0.0, 0.0, 0.0}, {-3.0, 0.0, 0.0}, 2, 0.001, 2});
    const std::vector<necwb::analysis::SegmentCurrentResult> unorderedCurrents{
        {.segment = 1, .wireTag = 1, .magnitude = 5.0},
        {.segment = 2, .wireTag = 1, .magnitude = 1.0},
        {.segment = 1, .wireTag = 2, .magnitude = 4.0},
        {.segment = 2, .wireTag = 2, .magnitude = 2.0}};
    const auto currentPaths = necwb::ui::connectedCurrentPaths(unorderedCurrents, currentAntenna);
    if (currentPaths.size() != 1
        || currentPaths.front().front().wireTag != 2
        || currentPaths.front().front().segment != 2
        || currentPaths.front()[1].segment != 1
        || currentPaths.front()[2].wireTag != 1
        || currentPaths.front()[2].segment != 1
        || currentPaths.front().back().segment != 2) {
        return NECWB_SMOKE_FAILURE("current distribution paths");
    }
    // Two joined wires that share tag 0 stay two wires, chained into one path.
    necwb::model::AntennaModel sharedTagAntenna;
    sharedTagAntenna.addWire({0, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, 2, 0.001, 1});
    sharedTagAntenna.addWire({0, {2.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, 2, 0.001, 2});
    const std::vector<necwb::analysis::SegmentCurrentResult> sharedTagCurrents{
        {.segment = 3, .wireTag = 0, .magnitude = 3.0, .necSegment = 3},
        {.segment = 1, .wireTag = 0, .magnitude = 1.0, .necSegment = 1},
        {.segment = 4, .wireTag = 0, .magnitude = 4.0, .necSegment = 4},
        {.segment = 2, .wireTag = 0, .magnitude = 2.0, .necSegment = 2}};
    const auto sharedTagPaths = necwb::ui::connectedCurrentPaths(sharedTagCurrents, sharedTagAntenna);
    auto sharedTagOrder = std::vector<int>{};
    for (const auto& path : sharedTagPaths)
        for (const auto& sample : path) sharedTagOrder.push_back(sample.necSegment);
    if (sharedTagPaths.size() != 1 || (sharedTagOrder != std::vector{1, 2, 3, 4}
            && sharedTagOrder != std::vector{4, 3, 2, 1}))
        return NECWB_SMOKE_FAILURE("current paths for wires that share a tag");
    currents.setModel(currentAntenna);
    currents.setResults(necwb::ui::ResultData::make(result), QStringLiteral("test-run"));
    currents.setSelectedFrequency(14.1);
    radiation2D.setResults(necwb::ui::ResultData::make(result), QStringLiteral("test-run"));
    auto radiationReference = result;
    for (auto& sample : radiationReference.radiation) sample.totalGainDb -= 1.0;
    radiation2D.setReferenceResults(necwb::ui::ResultData::make(radiationReference), QStringLiteral("Original dipole"));
    radiation3D.setReferenceResults(necwb::ui::ResultData::make(radiationReference), QStringLiteral("Original dipole"));
    radiationPerformance.setReferenceResults(
        necwb::ui::ResultData::make(radiationReference), QStringLiteral("Original dipole"));
    radiation2D.resize(800, 800);
    radiation2D.show();
    application.processEvents();
    auto* radiationPolarPlot = radiation2D.findChild<QWidget*>(
        QStringLiteral("radiationPolarPlot"));
    auto* radiationHoverReadout = radiation2D.findChild<QLabel*>(
        QStringLiteral("radiation2DHoverReadout"));
    if (radiationPolarPlot != nullptr) {
        const QPoint hoverPoint(radiationPolarPlot->width() / 2,
            radiationPolarPlot->height() / 4);
        QMouseEvent hoverEvent(QEvent::MouseMove, QPointF(hoverPoint),
            QPointF(radiationPolarPlot->mapToGlobal(hoverPoint)), Qt::NoButton,
            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(radiationPolarPlot, &hoverEvent);
        application.processEvents();
    }
    std::vector<QPointF> fullCircle;
    for (auto angle = 0; angle < 360; angle += 10) fullCircle.emplace_back(angle, 1.0);
    std::vector<QPointF> partialCircle;
    for (auto angle = 0; angle <= 90; angle += 10) partialCircle.emplace_back(angle, 1.0);
    if (radiationPolarPlot == nullptr
        || radiationHoverReadout == nullptr
        || !radiationHoverReadout->text().contains(QStringLiteral("Nearest NEC sample"))
        || !radiationHoverReadout->text().contains(QStringLiteral("dBi"))
        || !radiationHoverReadout->text().contains(QStringLiteral("Relative"))
        || radiationPolarPlot->property("hoverReadout").toString().isEmpty()
        || !radiationPolarPlot->property("closedPattern").toBool()
        || radiationPolarPlot->property("labelColorRole").toInt()
            != static_cast<int>(QPalette::Text)
        || !radiationPolarPlot->property("ringLabelBackground").toBool()
        || radiationPolarPlot->property("hoverSampleMode").toString()
            != QStringLiteral("nearest-nec-sample")
        || radiationPolarPlot->property("referenceVisible").toBool()
        || !necwb::ui::radiationAnglesCoverCircle(fullCircle)
        || necwb::ui::radiationAnglesCoverCircle(partialCircle)) {
        return NECWB_SMOKE_FAILURE("radiation cut analysis");
    }
    auto incompatibleRadiationReference = radiationReference;
    std::erase_if(incompatibleRadiationReference.radiation, [](const auto& sample) {
        return necwb::nearlyEqual(sample.frequencyMHz, 14.1)
            && sample.patternIndex == 0 && necwb::nearlyEqual(sample.phiDegrees, 0.0)
            && necwb::nearlyEqual(sample.thetaDegrees, 15.0);
    });
    radiation2D.setReferenceVisible(true);
    radiation2D.setReferenceResults(
        necwb::ui::ResultData::make(incompatibleRadiationReference), QStringLiteral("Different grid"));
    application.processEvents();
    auto* initialRadiationSummary = radiation2D.findChild<QLabel*>(
        QStringLiteral("radiation2DSummary"));
    if (radiationPolarPlot->property("referenceVisible").toBool()
        || initialRadiationSummary == nullptr
        || !initialRadiationSummary->text().contains(QStringLiteral("unavailable"))) {
        return NECWB_SMOKE_FAILURE("incompatible radiation reference grid");
    }
    radiation2D.setReferenceResults(necwb::ui::ResultData::make(radiationReference), QStringLiteral("Original dipole"));
    radiation2D.setReferenceVisible(false);
    necwb::model::AntennaModel antenna;
    antenna.addWire({1, {0.0, 0.0, -0.5}, {0.0, 0.0, 0.5}, 11, 0.001, 1});
    radiation3D.setModel(antenna);
    necwb::model::AntennaModel elevatedAntenna;
    elevatedAntenna.addWire({1, {-5.0, 0.0, 6.1}, {5.0, 0.0, 6.1}, 11, 0.001, 1});
    const auto resultOriginPreserved = std::abs(
        necwb::ui::resultModelExtentFromOrigin(elevatedAntenna) - 6.1) < 1.0e-12;
    necwb::model::AntennaModel curvedAntenna;
    auto curvedWire = necwb::model::Wire{2, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
        2, 0.001, 2};
    curvedWire.path = {{-1.0, 0.0, 0.0}, {0.0, 0.0, 4.0}, {1.0, 0.0, 0.0}};
    curvedAntenna.addWire(std::move(curvedWire));
    const auto semanticResultExtent = std::abs(
        necwb::ui::resultModelExtentFromOrigin(curvedAntenna) - 4.0) < 1.0e-12;
    radiation3D.setResults(necwb::ui::ResultData::make(result), QStringLiteral("test-run"));
    radiationPerformance.setResults(necwb::ui::ResultData::make(result), QStringLiteral("test-run"));
    radiationPerformance.setSelectedFrequency(14.2);
    radiation2D.setSettingsChangedCallback([&](const auto& settings) {
        radiation3D.setDisplaySettings(settings);
        radiationPerformance.setComponent(settings.component);
    });
    radiation3D.setSettingsChangedCallback([&](const auto& settings) {
        radiation2D.setDisplaySettings(settings);
        radiationPerformance.setComponent(settings.component);
    });
    radiationPerformance.setComponentChangedCallback([&](auto component) {
        radiation2D.setComponent(component);
        radiation3D.setComponent(component);
    });
    auto* radiation2DComponent = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DComponent"));
    auto* radiation2DShowReference = radiation2D.findChild<QCheckBox*>(
        QStringLiteral("radiation2DShowReference"));
    auto* radiation2DFrequency = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DFrequency"));
    auto* radiation3DComponent = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DComponent"));
    auto* radiation3DShowReference = radiation3D.findChild<QCheckBox*>(
        QStringLiteral("radiation3DShowReference"));
    auto* radiationPerformanceComponent = radiationPerformance.findChild<QComboBox*>(
        QStringLiteral("radiationPerformanceComponent"));
    auto* radiation3DFrequency = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DFrequency"));
    auto* radiation2DFloor = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DFloor"));
    auto* radiation3DFloor = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DFloor"));
    auto* radiation2DSummary = radiation2D.findChild<QLabel*>(QStringLiteral("radiation2DSummary"));
    auto* radiation2DCutPlane = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DCutPlane"));
    auto* radiation2DDataset = radiation2D.findChild<QComboBox*>(QStringLiteral("radiation2DDataset"));
    auto* radiation3DDataset = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DDataset"));
    auto* radiation2DOrientation = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DOrientation"));
    const auto radiation2DShortcuts = radiation2D.findChildren<QShortcut*>();
    auto* radiation2DMaxGainCut = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DMaxGainCut"));
    auto* radiation3DSummary = radiation3D.findChild<QLabel*>(QStringLiteral("radiation3DSummary"));
    auto* radiation3DHoverReadout = radiation3D.findChild<QLabel*>(
        QStringLiteral("radiation3DHoverReadout"));
    auto* radiation3DSurface = radiation3D.findChild<QWidget*>(
        QStringLiteral("radiation3DSurface"));
    auto* radiation2DExportImage = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DExportImage"));
    auto* radiation2DExportData = radiation2D.findChild<QPushButton*>(QStringLiteral("radiation2DExportData"));
    auto* radiation3DExportImage = radiation3D.findChild<QPushButton*>(QStringLiteral("radiation3DExportImage"));
    auto* radiation3DExportData = radiation3D.findChild<QPushButton*>(QStringLiteral("radiation3DExportData"));
    auto* radiationPerformanceSummary = radiationPerformance.findChild<QLabel*>(
        QStringLiteral("radiationPerformanceSummary"));
    auto* radiationPerformanceShowReference = radiationPerformance.findChild<QCheckBox*>(
        QStringLiteral("radiationPerformanceShowReference"));
    auto* radiationPerformancePlot = radiationPerformance.findChild<QWidget*>(
        QStringLiteral("directionalMetricsPlot"));
    auto* radiationPerformanceElevation = radiationPerformance.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationPerformanceElevation"));
    auto* radiationPerformanceAzimuth = radiationPerformance.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationPerformanceAzimuth"));
    auto* radiationPerformanceUsePatternPeak = radiationPerformance.findChild<QPushButton*>(
        QStringLiteral("radiationPerformanceUsePatternPeak"));
    if (radiation2DComponent == nullptr || radiation2DFrequency == nullptr
        || radiation3DComponent == nullptr || radiation3DFrequency == nullptr
        || radiationPerformanceComponent == nullptr
        || radiation2DFloor == nullptr || radiation3DFloor == nullptr
        || radiation2DSummary == nullptr || radiation3DSummary == nullptr
        || radiation3DHoverReadout == nullptr || radiation3DSurface == nullptr
        || radiation2DCutPlane == nullptr || radiation2DOrientation == nullptr
        || radiation2DDataset == nullptr || radiation3DDataset == nullptr
        || radiation2DMaxGainCut == nullptr
        || radiation2DShowReference == nullptr || radiation3DShowReference == nullptr
        || radiationPerformanceShowReference == nullptr
        || radiationPerformanceSummary == nullptr || radiationPerformancePlot == nullptr
        || radiationPerformanceElevation == nullptr
        || radiationPerformanceAzimuth == nullptr
        || radiationPerformanceUsePatternPeak == nullptr
        || radiation2DExportImage == nullptr || radiation2DExportData == nullptr
        || radiation3DExportImage == nullptr || radiation3DExportData == nullptr)
        return NECWB_SMOKE_FAILURE("radiation result controls");
    necwb::analysis::AnalysisResult directionalSeedResult;
    directionalSeedResult.radiation = {
        {14.175, 90.0, 0.0, -999.99, -999.99, -999.99},
        {14.175, 85.0, 0.0, -17.94, -999.99, -17.94},
        {14.175, 15.0, 90.0, 5.86, -999.99, 5.86},
    };
    necwb::ui::RadiationPerformanceView directionalSeedView;
    directionalSeedView.setResults(necwb::ui::ResultData::make(directionalSeedResult), QStringLiteral("direction-test"));
    directionalSeedView.setSelectedFrequency(14.175);
    auto* seededElevation = directionalSeedView.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationPerformanceElevation"));
    auto* seededAzimuth = directionalSeedView.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationPerformanceAzimuth"));
    auto* seededSummary = directionalSeedView.findChild<QLabel*>(
        QStringLiteral("radiationPerformanceSummary"));
    auto* seededPeakButton = directionalSeedView.findChild<QPushButton*>(
        QStringLiteral("radiationPerformanceUsePatternPeak"));
    if (seededElevation == nullptr || seededAzimuth == nullptr
        || seededSummary == nullptr || seededPeakButton == nullptr) {
        return NECWB_SMOKE_FAILURE("directional performance seed controls");
    }
    const auto peakDirectionSeeded = seededElevation->value() == 75.0
        && seededAzimuth->value() == 90.0
        && seededSummary->text().contains(QStringLiteral("NEC θ 15"));
    seededElevation->setValue(0.0);
    seededAzimuth->setValue(0.0);
    const auto patternNullExplained = seededSummary->text().contains(
        QStringLiteral("pattern null"));
    seededElevation->setValue(42.0);
    seededAzimuth->setValue(47.0);
    const auto missingDirectionExplained = seededSummary->text().contains(
        QStringLiteral("no matching RP sample"));
    seededPeakButton->click();
    const auto patternPeakRestored = seededElevation->value() == 75.0
        && seededAzimuth->value() == 90.0;
    const auto radiationReferencesDefaultHidden = !radiation2DShowReference->isChecked()
        && !radiation3DShowReference->isChecked()
        && !radiationPerformanceShowReference->isChecked();
    radiation2DShowReference->setChecked(true);
    radiation3DShowReference->setChecked(true);
    radiationPerformanceShowReference->setChecked(true);
    application.processEvents();
    const auto radiation2DReferenceVisible = radiationPolarPlot->property(
        "referenceVisible").toBool();
    const auto radiation3DReferenceVisible = radiation3DSurface->property(
        "referenceVisible").toBool();
    const auto radiationPerformanceReferenceCount = radiationPerformancePlot->property(
        "referenceSeriesCount").toInt();
    const auto radiationReferencesVisible = radiation2DReferenceVisible
        && radiation3DReferenceVisible && radiationPerformanceReferenceCount > 0;
    radiation2DShowReference->setChecked(false);
    radiation3DShowReference->setChecked(false);
    radiationPerformanceShowReference->setChecked(false);
    application.processEvents();
    const auto radiationReferencesCanHide = !radiationPolarPlot->property(
            "referenceVisible").toBool()
        && !radiation3DSurface->property("referenceVisible").toBool()
        && radiationPerformancePlot->property("referenceSeriesCount").toInt() == 0;
    radiation2DShowReference->setChecked(true);
    radiation3DShowReference->setChecked(true);
    radiationPerformanceShowReference->setChecked(true);
    if (!radiationReferencesDefaultHidden || !radiationReferencesVisible
        || !radiationReferencesCanHide)
        return NECWB_SMOKE_FAILURE("radiation reference visibility controls");
    // Space belongs to the pattern only while it has focus: in the main window the key is
    // also used by the geometry view, buttons and text fields.
    const auto spaceChangesCutInsideView = std::ranges::any_of(
        radiation2DShortcuts, [](const QShortcut* shortcut) {
            return shortcut->key() == QKeySequence(Qt::Key_Space)
                && shortcut->context() == Qt::WidgetWithChildrenShortcut;
        });
    radiation3D.resize(900, 700);
    radiation3D.show();
    application.processEvents();
    const auto legendColorCount = [](const QImage& image) {
        auto count = 0;
        for (auto y = 0; y < image.height(); ++y) {
            for (auto x = 0; x < image.width(); ++x) {
                if (QColor::fromRgba(image.pixel(x, y)).hsvSaturation() > 80) ++count;
            }
        }
        return count;
    };
    const auto legendRegion = QRect(10, 42, 285, 108);
    const auto legendColorsBeforeHover = legendColorCount(
        radiation3DSurface->grab(legendRegion).toImage());
    const QPoint radiation3DHoverPoint(radiation3DSurface->width() / 2,
        radiation3DSurface->height() / 3);
    QMouseEvent radiation3DHoverEvent(QEvent::MouseMove, QPointF(radiation3DHoverPoint),
        QPointF(radiation3DSurface->mapToGlobal(radiation3DHoverPoint)), Qt::NoButton,
        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(radiation3DSurface, &radiation3DHoverEvent);
    application.processEvents();
    const auto legendColorsDuringHover = legendColorCount(
        radiation3DSurface->grab(legendRegion).toImage());
    const auto pitchBeforeDrag = radiation3DSurface->property("viewPitch").toDouble();
    const auto dragStart = QPointF(radiation3DSurface->width() * 0.75,
        radiation3DSurface->height() * 0.75);
    QMouseEvent radiation3DPress(QEvent::MouseButtonPress, dragStart,
        QPointF(radiation3DSurface->mapToGlobal(dragStart.toPoint())), Qt::LeftButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(radiation3DSurface, &radiation3DPress);
    const auto dragEnd = dragStart - QPointF(0.0, 30.0);
    QMouseEvent radiation3DDrag(QEvent::MouseMove, dragEnd,
        QPointF(radiation3DSurface->mapToGlobal(dragEnd.toPoint())), Qt::NoButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(radiation3DSurface, &radiation3DDrag);
    QMouseEvent radiation3DRelease(QEvent::MouseButtonRelease, dragEnd,
        QPointF(radiation3DSurface->mapToGlobal(dragEnd.toPoint())), Qt::LeftButton,
        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(radiation3DSurface, &radiation3DRelease);
    application.processEvents();
    const auto verticalOrbitTracksPointer =
        radiation3DSurface->property("viewPitch").toDouble() > pitchBeforeDrag;
    const auto radiation3DProbeVisible = radiation3DHoverReadout->text().contains(
            QStringLiteral("Nearest NEC sample"))
        && radiation3DHoverReadout->text().contains(QStringLiteral("θ"))
        && radiation3DHoverReadout->text().contains(QStringLiteral("φ"))
        && radiation3DHoverReadout->text().contains(QStringLiteral("dBi"))
        && radiation3DSurface->property("hoverReadout").toString()
            == radiation3DHoverReadout->text();
    const auto radiation3DColorScaleVisible = radiation3DSurface->property(
            "gainColorScale").toBool()
        && radiation3DSurface->property("colorScaleMinimum").isValid()
        && radiation3DSurface->property("colorScaleMaximum").isValid()
        && radiation3DSurface->property("currentColorScale").toBool()
        && radiation3DSurface->property("currentColorScaleMinimum").toDouble() == 0.0
        && radiation3DSurface->property("currentColorScaleMaximum").toDouble() > 0.0
        && radiation3DSurface->property("currentColorScaleTop").toDouble() >= 100.0
        && legendColorsBeforeHover > 100
        && legendColorsDuringHover == legendColorsBeforeHover
        && verticalOrbitTracksPointer;
    // The surface: shaded with its grid, solid or see-through, or the wireframe, which has
    // nothing to see through.
    auto* radiation3DStyle = radiation3D.findChild<QComboBox*>(QStringLiteral("radiation3DStyle"));
    auto* radiation3DSeeThrough = radiation3D.findChild<QCheckBox*>(
        QStringLiteral("radiation3DSeeThrough"));
    auto radiation3DLook = radiation3DStyle != nullptr && radiation3DSeeThrough != nullptr
        && radiation3DStyle->count() == 3;
    if (radiation3DLook) {
        radiation3DStyle->setCurrentIndex(0);
        radiation3DSeeThrough->setChecked(false);
        const auto shadedSolid = radiation3DSurface->property("surfaceStyle").toInt() == 0
            && !radiation3DSurface->property("seeThrough").toBool();
        radiation3DStyle->setCurrentIndex(2);
        const auto wireframe = radiation3DSurface->property("surfaceStyle").toInt() == 2
            && !radiation3DSeeThrough->isEnabled();
        radiation3DStyle->setCurrentIndex(0);
        radiation3DSeeThrough->setChecked(true);
        radiation3DLook = shadedSolid && wireframe && radiation3DSeeThrough->isEnabled()
            && radiation3DSurface->property("seeThrough").toBool();
    }
    // A θ×φ grid fills with patches; a single cut has nothing to fill.
    NECWB_SMOKE_STEP();
    {
        std::vector<necwb::ui::SurfaceSample> grid, cut;
        for (auto theta = 0; theta <= 180; theta += 45)
            for (auto phi = 0; phi < 360; phi += 45) {
                const auto t = theta * std::numbers::pi / 180.0, f = phi * std::numbers::pi / 180.0;
                const necwb::ui::SurfaceSample sample{double(theta), double(phi),
                    {std::sin(t) * std::cos(f), std::sin(t) * std::sin(f), std::cos(t)}, 0.8};
                grid.push_back(sample);
                if (phi == 0) cut.push_back(sample);
            }
        QImage canvas(200, 200, QImage::Format_ARGB32);
        canvas.fill(Qt::white);
        QPainter painter(&canvas);
        const necwb::ui::SurfaceView view{0.3, 0.4, 80.0, QPointF(100, 100)};
        const auto cutDrawn = necwb::ui::drawRadiationSurface(painter, cut, view, true, false, 1000);
        const auto gridDrawn = necwb::ui::drawRadiationSurface(painter, grid, view, true, false, 1000);
        painter.end();
        // Wires sort in with a solid surface: one inside it is hidden, one in front of it
        // shows over it.
        QImage sorted(400, 400, QImage::Format_ARGB32);
        sorted.fill(Qt::white);
        QPainter sortedPainter(&sorted);
        const necwb::ui::SurfaceView wide{0.3, 0.4, 80.0, QPointF(200, 200)};
        // Toward the viewer, and right across the screen.
        const necwb::model::Point3D near{-1.8 * std::cos(0.4) * std::sin(0.3),
            -1.8 * std::cos(0.4) * std::cos(0.3), -1.8 * std::sin(0.4)};
        const necwb::model::Point3D right{0.15 * std::cos(0.3), -0.15 * std::sin(0.3), 0};
        const QPen blue(QColor(0, 0, 255), 4);
        const std::vector<necwb::ui::SurfaceLine> wires{{{0, 0, 0}, {0.6, 0, 0}, blue},
            {{near.x - right.x, near.y - right.y, near.z}, {near.x + right.x, near.y + right.y, near.z}, blue}};
        necwb::ui::drawRadiationSurface(sortedPainter, grid, wide, false, false, 4000, wires);
        sortedPainter.end();
        const auto front = wide.project(near).toPoint();
        const auto inside = wide.project({0.4, 0, 0}).toPoint();
        const auto wireSorted = sorted.pixelColor(front) == QColor(0, 0, 255)
            && sorted.pixelColor(inside) != QColor(0, 0, 255);
        if (!wireSorted) qWarning() << "wire sorting" << sorted.pixelColor(front) << sorted.pixelColor(inside);
        radiation3DLook = radiation3DLook && !cutDrawn && gridDrawn && wireSorted
            && canvas.pixelColor(100, 100) != QColor(Qt::white);
    }
    // Fitting: a lobe off to one side is centred and fills the area; the view fits until
    // a zoom, and Reset View fits again. The controls wrap to a narrow centre.
    NECWB_SMOKE_STEP();
    {
        const std::vector<necwb::model::Point3D> lobe{{0, 0, 0}, {1.0, 0.2, 0.1}, {0.6, -0.2, -0.1}};
        const QRectF area(20, 100, 400, 300);
        const auto fitted = necwb::ui::fitSurfaceView(lobe, -0.7, 0.45, area);
        auto inside = true;
        QRectF bounds;
        for (const auto& point : lobe) {
            const auto screen = fitted.project(point);
            inside = inside && area.adjusted(-0.01, -0.01, 0.01, 0.01).contains(screen);
            bounds = bounds.united(QRectF(screen, QSizeF(0.001, 0.001)));
        }
        const auto fills = std::abs(bounds.width() - area.width()) < 0.5
            || std::abs(bounds.height() - area.height()) < 0.5;
        auto* resetView = [&radiation3D]() -> QPushButton* {
            for (auto* button : radiation3D.findChildren<QPushButton*>())
                if (button->text() == QStringLiteral("Reset View")) return button;
            return nullptr;
        }();
        const necwb::ui::Radiation3DView fresh;
        const auto fitsAtStart = fresh.findChild<QWidget*>(QStringLiteral("radiation3DSurface"))
            ->property("autoFit").toBool();
        QWheelEvent wheel(QPointF(50, 50), radiation3DSurface->mapToGlobal(QPointF(50, 50)), {},
            {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(radiation3DSurface, &wheel);
        const auto zoomStopsFit = !radiation3DSurface->property("autoFit").toBool();
        if (resetView != nullptr) resetView->click();
        const auto resetFits = radiation3DSurface->property("autoFit").toBool();
        const auto narrow = radiation3D.minimumSizeHint().width() <= 560;
        QWidget host;
        auto* flow = new necwb::ui::FlowLayout(&host);
        auto* first = new QWidget(&host);
        auto* second = new QWidget(&host);
        first->setFixedSize(100, 20);
        second->setFixedSize(100, 20);
        flow->addWidget(first);
        flow->addStretch();
        flow->addWidget(second);
        host.show();
        const auto wraps = flow->heightForWidth(250) == 20 && flow->heightForWidth(150) == 46;
        flow->setGeometry(QRect(0, 0, 150, 46));
        const auto rightAligned = second->geometry() == QRect(50, 26, 100, 20);
        const auto fitted3D = inside && fills && fitsAtStart && zoomStopsFit && resetFits
            && narrow && wraps && rightAligned;
        if (!fitted3D)
            qWarning() << "3D fit" << inside << fills << fitsAtStart << zoomStopsFit << resetFits
                       << radiation3D.minimumSizeHint() << flow->heightForWidth(150) << second->geometry();
        radiation3DLook = radiation3DLook && fitted3D;
    }
    radiation2DComponent->setCurrentIndex(1);
    radiation2D.setSelectedFrequency(14.2);
    radiation3D.setSelectedFrequency(14.2);
    radiation3DFloor->setCurrentIndex(4);
    application.processEvents();
    const auto radiationSweepSelectable = radiation2DFrequency->count() == 2
        && radiation2DFrequency->currentData().toDouble() == 14.2
        && radiation3DFrequency->currentData().toDouble() == 14.2
        && !radiation2DFrequency->isHidden() && !radiation3DFrequency->isHidden()
        && radiation2DFrequency->itemText(1).contains(QStringLiteral("2 pattern"));
    radiation2D.setSelectedFrequency(99.0);
    const auto nonPatternFrequencyIgnored = radiation2DFrequency->count() == 2
        && radiation2DFrequency->currentData().toDouble() == 14.2;
    radiation2DFrequency->setCurrentIndex(0);
    application.processEvents();
    const auto radiationFrequencySynchronized =
        radiation3DFrequency->currentData().toDouble() == 14.1;
    radiation2DFrequency->setCurrentIndex(1);
    application.processEvents();
    const auto mixedPatternCutsSeparated = radiation2DCutPlane->count() == 6
        && radiation2DCutPlane->itemData(0).toDouble() == 0.0
        && radiation2DCutPlane->itemData(1).toDouble() == 30.0
        && radiation2DDataset->count() == 2
        && radiation3DDataset->count() == 2
        && radiation3DDataset->currentData().toInt() == 0;
    radiation2DDataset->setCurrentIndex(radiation2DDataset->findData(1));
    application.processEvents();
    const auto horizontalDatasetSelectsCompatibleCut = radiation2DOrientation->isEnabled()
        && radiation2DOrientation->text() == QStringLiteral("Show Elevation Cut")
        && radiation2DCutPlane->count() == 1
        && radiation2DCutPlane->currentData().toDouble() == 62.0
        && radiation2DSummary->text().contains(QStringLiteral("peak"));
    radiation2DOrientation->click();
    application.processEvents();
    const auto separateDatasetsSwitchCuts = radiation2DDataset->currentData().toInt() == 0
        && radiation2DOrientation->text() == QStringLiteral("Show Azimuth Cut")
        && radiation2DCutPlane->count() == 6;
    radiation2DDataset->setCurrentIndex(radiation2DDataset->findData(1));
    application.processEvents();
    radiation2DMaxGainCut->click();
    application.processEvents();
    const auto horizontalMaxGainCutStaysCompatible = radiation2DOrientation->text()
        == QStringLiteral("Show Elevation Cut") && radiation2DCutPlane->currentData().toDouble() == 62.0;
    radiation2DDataset->setCurrentIndex(radiation2DDataset->findData(0));
    application.processEvents();
    const auto radiationControlsSynchronized = radiation3DComponent->currentData()
            == radiation2DComponent->currentData()
        && radiationPerformanceComponent->currentData() == radiation2DComponent->currentData()
        && radiation2DFloor->currentData() == radiation3DFloor->currentData();
    const auto radiationMetricsVisible = radiation2DSummary->text().contains(QStringLiteral("HPBW"))
        && radiation3DSummary->text().contains(QStringLiteral("peak"))
        && radiation3DProbeVisible && radiation3DColorScaleVisible && radiation3DLook
        && radiationPerformanceSummary->text().contains(QStringLiteral("Gain 2/2"))
        && radiationPerformanceSummary->text().contains(QStringLiteral("F/B 2/2"))
        && radiationPerformancePlot->property("pointCount").toInt() == 2
        && radiationPerformancePlot->property("seriesCount").toInt() == 6
        && radiationPerformancePlot->property("referenceSeriesCount").toInt() == 3
        && radiationPerformancePlot->property("seriesStyles").toStringList()
            == QStringList{QString::number(static_cast<int>(Qt::DashLine)),
                QString::number(static_cast<int>(Qt::DashLine)),
                QString::number(static_cast<int>(Qt::DashLine)),
                QString::number(static_cast<int>(Qt::SolidLine)),
                QString::number(static_cast<int>(Qt::SolidLine)),
                QString::number(static_cast<int>(Qt::DashLine))}
        && radiationPerformancePlot->property("selectedFrequencyMHz").toDouble() == 14.2;
    const auto radiationExportReady = radiation2DExportImage->isEnabled()
        && radiation2DExportData->isEnabled() && radiation3DExportImage->isEnabled()
        && radiation3DExportData->isEnabled();
    // Go to Peak keeps the kind of cut: elevation at the peak's azimuth (phi 60),
    // azimuth at the peak's elevation (theta 75).
    radiation2DOrientation->click();
    radiation2DMaxGainCut->click();
    application.processEvents();
    const auto elevationMaxGainCutSelected = radiation2DOrientation->text()
            == QStringLiteral("Show Azimuth Cut")
        && radiation2DCutPlane->currentData().toDouble() == 60.0
        && radiation2DSummary->text().contains(QStringLiteral("20.000 dBi"));
    radiation2DOrientation->click();
    radiation2DMaxGainCut->click();
    application.processEvents();
    const auto maxGainCutSelected = elevationMaxGainCutSelected
        && radiation2DOrientation->text() == QStringLiteral("Show Elevation Cut")
        && radiation2DCutPlane->currentData().toDouble() == 75.0
        && radiation2DSummary->text().contains(QStringLiteral("Azimuth cut at El 15°"))
        && radiation2DSummary->text().contains(QStringLiteral("20.000 dBi"));
    necwb::analysis::AnalysisResult signedThetaResult;
    for (auto theta = -90; theta <= 90; theta += 10) {
        for (const auto phi : {0.0, 180.0}) {
            const auto gain = theta == -70 ? 8.2 : -20.0;
            signedThetaResult.radiation.push_back({14.3, static_cast<double>(theta), phi,
                gain, -999.99, gain});
        }
    }
    necwb::ui::RadiationPatternView signedThetaView;
    signedThetaView.setResults(necwb::ui::ResultData::make(signedThetaResult), QStringLiteral("signed-theta-test"));
    auto* signedThetaSummary = signedThetaView.findChild<QLabel*>(
        QStringLiteral("radiation2DSummary"));
    const auto signedThetaTiesNormalized = signedThetaSummary != nullptr
        && signedThetaSummary->text().contains(QStringLiteral("Elevation cut at Az 0°"))
        && signedThetaSummary->text().contains(QStringLiteral("Peak in this cut 8.200 dBi @ El 20°, Az 180°"))
        && signedThetaSummary->text().contains(QStringLiteral("tied at El 20°, Az 0°"))
        && !signedThetaSummary->text().contains(QStringLiteral("430"));
    auto* signedThetaPlot = signedThetaView.findChild<QWidget*>(
        QStringLiteral("radiationPolarPlot"));
    const auto signedThetaCutStaysOpen = signedThetaPlot != nullptr
        && !signedThetaPlot->property("closedPattern").toBool();
    // A cut that misses the main lobe names its own maximum and points at the pattern peak.
    necwb::analysis::AnalysisResult offCutPeakResult;
    for (auto theta = -90; theta <= 90; theta += 10) {
        const auto gain = theta == 80 ? 5.0 : -10.0;
        offCutPeakResult.radiation.push_back({146.0, static_cast<double>(theta), 0.0,
            gain, -999.99, gain});
    }
    for (auto phi = 0; phi < 360; phi += 30) {
        const auto gain = phi == 90 ? 8.0 : 0.0;
        offCutPeakResult.radiation.push_back({146.0, 80.0, static_cast<double>(phi),
            gain, -999.99, gain, {}, {}, {}, 1});
    }
    necwb::ui::RadiationPatternView offCutPeakView;
    offCutPeakView.setResults(necwb::ui::ResultData::make(offCutPeakResult), QStringLiteral("off-cut-test"));
    auto* offCutSummary = offCutPeakView.findChild<QLabel*>(QStringLiteral("radiation2DSummary"));
    auto* offCutPlot = offCutPeakView.findChild<QWidget*>(QStringLiteral("radiationPolarPlot"));
    const auto offCutPeakNamed = offCutSummary != nullptr && offCutPlot != nullptr
        && offCutSummary->text().contains(QStringLiteral("Elevation cut at Az 0°"))
        && offCutSummary->text().contains(QStringLiteral(
            "Peak in this cut 5.000 dBi @ El 10°, Az 0° (sampled, RP step 10°)"))
        && offCutSummary->text().contains(QStringLiteral(
            "Sampled peak (all RP data) 8.000 dBi @ El 10°, Az 90° (sampled, RP step 30°), outside this cut"))
        && offCutSummary->toolTip().contains(QStringLiteral("F/B:"))
        && offCutPlot->property("cutName").toString() == QStringLiteral("Elevation cut at Az 0°");
    necwb::ui::RadiationPerformanceView offCutPerformance;
    offCutPerformance.setResults(necwb::ui::ResultData::make(offCutPeakResult), QStringLiteral("off-cut-test"));
    offCutPerformance.setSelectedFrequency(146.0);
    offCutPerformance.setForwardDirection(80.0, 90.0);
    auto* offCutReadout = offCutPerformance.findChild<QLabel*>(
        QStringLiteral("radiationPerformanceReadout"));
    auto* offCutElevation = offCutPerformance.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationPerformanceElevation"));
    const auto forwardDirectionReadout = offCutReadout != nullptr && offCutElevation != nullptr
        && offCutElevation->value() == 10.0
        && offCutReadout->text().contains(QStringLiteral("Gain toward El 10°, Az 90°: 8.000 dBi"))
        && offCutReadout->toolTip().contains(QStringLiteral("F/R:"));
    necwb::analysis::AnalysisResult crossingCutsResult;
    const auto lobe = [](double theta, double phi) {
        const auto azimuthOffset = std::abs(std::remainder(phi, 360.0));
        return 10.0 - 0.3 * std::abs(theta - 80.0) - 0.05 * azimuthOffset;
    };
    for (auto theta = 0; theta <= 90; theta += 5) {
        for (auto phi = 0; phi < 360; phi += 5) {
            const auto gain = lobe(theta, phi);
            crossingCutsResult.radiation.push_back({146.0, static_cast<double>(theta),
                static_cast<double>(phi), gain, -999.99, gain});
        }
    }
    for (auto phi = 0; phi < 360; ++phi) {
        const auto gain = lobe(80.0, phi);
        crossingCutsResult.radiation.push_back({146.0, 80.0, static_cast<double>(phi),
            gain, -999.99, gain, {}, {}, {}, 1});
    }
    for (auto theta = 0; theta <= 90; ++theta) {
        for (const auto phi : {0.0, 180.0}) {
            const auto gain = lobe(theta, phi);
            crossingCutsResult.radiation.push_back({146.0, static_cast<double>(theta), phi,
                gain, -999.99, gain, {}, {}, {}, 2});
        }
    }
    necwb::ui::RadiationPatternView crossingCutsView;
    crossingCutsView.setResults(necwb::ui::ResultData::make(crossingCutsResult), QStringLiteral("crossing-test"));
    auto* crossingDataset = crossingCutsView.findChild<QComboBox*>(
        QStringLiteral("radiation2DDataset"));
    auto* crossingPlane = crossingCutsView.findChild<QComboBox*>(
        QStringLiteral("radiation2DCutPlane"));
    auto* crossingToggle = crossingCutsView.findChild<QPushButton*>(
        QStringLiteral("radiation2DOrientation"));
    auto* crossingSummary = crossingCutsView.findChild<QLabel*>(
        QStringLiteral("radiation2DSummary"));
    auto crossingCutsPair = false;
    if (crossingDataset != nullptr && crossingPlane != nullptr && crossingToggle != nullptr
        && crossingSummary != nullptr) {
        crossingDataset->setCurrentIndex(crossingDataset->findData(2));
        application.processEvents();
        // An elevation-cut dataset no longer offers two-point "azimuth cuts" of its own.
        const auto elevationOnly = crossingToggle->text() == QStringLiteral("Show Azimuth Cut")
            && crossingToggle->isEnabled()
            && crossingToggle->toolTip().contains(QStringLiteral("RP 2"));
        crossingToggle->click();
        application.processEvents();
        const auto toFineAzimuth = crossingDataset->currentData().toInt() == 1
            && crossingPlane->currentData().toDouble() == 80.0
            && crossingSummary->text().startsWith(QStringLiteral("Switched to RP 2"));
        // The note is one-time: the next change clears it.
        crossingCutsView.findChild<QComboBox*>(QStringLiteral("radiation2DScale"))
            ->setCurrentIndex(1);
        application.processEvents();
        const auto switchNoteClears = !crossingSummary->text().contains(
            QStringLiteral("Switched to"));
        crossingToggle->click();
        application.processEvents();
        crossingCutsPair = elevationOnly && toFineAzimuth && switchNoteClears
            && crossingDataset->currentData().toInt() == 2
            && crossingPlane->currentData().toDouble() == 0.0;
    }
    necwb::ui::RadiationPatternView emptyRadiation2D;
    emptyRadiation2D.setResults({}, QStringLiteral("empty-radiation-test"));
    auto* emptyRadiationFrequency = emptyRadiation2D.findChild<QComboBox*>(
        QStringLiteral("radiation2DFrequency"));
    auto* emptyRadiationSummary = emptyRadiation2D.findChild<QLabel*>(
        QStringLiteral("radiation2DSummary"));
    auto* emptyRadiationExport = emptyRadiation2D.findChild<QPushButton*>(
        QStringLiteral("radiation2DExportData"));
    const auto missingRadiationReported = emptyRadiationFrequency != nullptr
        && emptyRadiationFrequency->count() == 0 && emptyRadiationSummary != nullptr
        && emptyRadiationSummary->text().contains(QStringLiteral("No"))
        && emptyRadiationExport != nullptr && !emptyRadiationExport->isEnabled();
    currents.show(); radiation2D.show(); radiation3D.show(); application.processEvents();
    QImage fieldImage(800, 600, QImage::Format_ARGB32_Premultiplied);
    fieldImage.fill(Qt::transparent); QPainter fieldPainter(&fieldImage);
    currents.render(&fieldPainter); radiation2D.render(&fieldPainter); radiation3D.render(&fieldPainter);
    fieldPainter.end();
    necwb::analysis::AnalysisResult currentOnly;
    currentOnly.currents = result.currents;
    radiation3D.setResults(necwb::ui::ResultData::make(currentOnly), QStringLiteral("Model: test.nec · Run: current-only"));
    radiation3D.setSelectedFrequency(14.1);
    application.processEvents();
    const auto missingRadiationDisablesDataExport = !radiation3DExportData->isEnabled()
        && radiation3DExportImage->isEnabled();
    QImage currentImage(800, 600, QImage::Format_ARGB32_Premultiplied);
    currentImage.fill(Qt::transparent); QPainter currentPainter(&currentImage);
    radiation3D.render(&currentPainter); currentPainter.end();
    // The model tree: group summaries, a row per item, the studies with Run buttons, and
    // selection that the window can hold back while an edit is unfinished.
    necwb::ui::ModelColumn modelColumn;
    auto* modelTree = modelColumn.tree();
    auto* propertiesPane = modelColumn.pane();
    using Study = necwb::ui::ModelTree::Study;
    using StudyState = necwb::ui::ModelTree::StudyState;
    using necwb::ui::ModelNode;
    using necwb::ui::ModelSelection;
    const auto modelTreeCompact = hasCompactMargins(modelColumn, 0);
    modelColumn.resize(460, 700);
    necwb::model::ModelSetup setup;
    setup.frequency = {0, 3, 14.0, 0.1, 1};
    setup.excitations = {{0, 1, 6, 1.0, 0.0, 3}};
    setup.radiationPatterns = {{19, 37, 0.0, 0.0, 5.0, 10.0, 5}};
    const auto modelTreeDocument = necwb::nec::NecParser{}.parse(
        "GW 1 11 0 0 -.5 0 0 .5 .001\nGE 0\nEX 0 1 6 0 1 0\n"
        "FR 0 3 0 0 14 .1\nRP 0 19 37 1000 0 0 5 10\nEN\n");
    modelTree->setModel(antenna, setup, modelTreeDocument);
    std::vector<ModelNode> addedTo;
    modelTree->setAddCallback([&](ModelNode node) { addedTo.push_back(node); });
    QAction averageGainAction(QStringLiteral("Run Average Gain Test…"), &modelColumn),
        convergenceAction(QStringLiteral("Segmentation Convergence…"), &modelColumn);
    // Run calls the tree's own callback, not the menu command that also opens the view.
    auto treeAverageGainTriggered = false, treeConvergenceTriggered = false, menuTriggered = false;
    modelTree->setStudyAction(Study::AverageGain, &averageGainAction,
        [&] { treeAverageGainTriggered = true; });
    modelTree->setStudyAction(Study::Convergence, &convergenceAction,
        [&] { treeConvergenceTriggered = true; });
    for (auto* action : {&averageGainAction, &convergenceAction})
        QObject::connect(action, &QAction::triggered, [&] { menuTriggered = true; });
    std::vector<ModelSelection> reportedSelections;
    std::optional<ModelSelection> activatedSelection;
    auto allowSelectionChange = true;
    modelTree->setSelectionChangingCallback([&](const ModelSelection&) { return allowSelectionChange; });
    modelTree->setSelectionChangedCallback(
        [&](const ModelSelection& selection) { reportedSelections.push_back(selection); });
    modelTree->setActivatedCallback(
        [&](const ModelSelection& selection) { activatedSelection = selection; });
    modelColumn.show();
    application.processEvents();
    const auto group = [modelTree](const QString& name) -> QTreeWidgetItem* {
        for (auto index = 0; index < modelTree->topLevelItemCount(); ++index)
            if (modelTree->topLevelItem(index)->text(0) == name) return modelTree->topLevelItem(index);
        return nullptr;
    };
    const auto summary = [&group](const QString& name) {
        const auto* item = group(name);
        return item == nullptr ? QString{} : item->text(1);
    };
    QStringList groupNames;
    for (auto index = 0; index < modelTree->topLevelItemCount(); ++index)
        groupNames.append(modelTree->topLevelItem(index)->text(0));
    const auto modelTreeGroups = groupNames == QStringList{QStringLiteral("Frequency"),
        QStringLiteral("Wires"), QStringLiteral("Sources"), QStringLiteral("Loads"),
        QStringLiteral("Trans. lines"), QStringLiteral("Networks"), QStringLiteral("Ground"),
        QStringLiteral("Requests"), QStringLiteral("Z0"), QStringLiteral("Other"),
        QStringLiteral("Variables"), QStringLiteral("Deck"), QStringLiteral("Validation")};
    // Counts only, the detail in tooltips; an empty group is its name alone, dim.
    const auto dim = modelTree->palette().color(QPalette::PlaceholderText);
    const auto* loadsGroup = group(QStringLiteral("Loads"));
    const auto modelTreeSummaries = summary(QStringLiteral("Wires")) == QStringLiteral("1")
        && group(QStringLiteral("Wires"))->toolTip(1) == QStringLiteral("1 wire · 11 segments")
        && summary(QStringLiteral("Sources")) == QStringLiteral("1")
        && summary(QStringLiteral("Other")) == QStringLiteral("3")
        && group(QStringLiteral("Other"))->toolTip(0) == QStringLiteral("3 cards: GE, FR, RP")
        && summary(QStringLiteral("Deck")) == QStringLiteral("6")
        && summary(QStringLiteral("Loads")).isEmpty() && loadsGroup->foreground(0).color() == dim
        && loadsGroup->toolTip(0) == QStringLiteral("No loads. Right-click to add a load.")
        && group(QStringLiteral("Wires"))->foreground(0).color() != dim
        && group(QStringLiteral("Wires"))->childCount() == 1
        && group(QStringLiteral("Wires"))->child(0)->text(0) == QStringLiteral("Wire 1")
        && group(QStringLiteral("Sources"))->childCount() == 1
        && group(QStringLiteral("Validation"))->childCount() == 2;
    // Right-click Add on an empty group, on a row of a full one, and none on Deck.
    const auto rightClick = [&](QTreeWidgetItem* item) {
        QTimer::singleShot(0, [] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
                if (!menu->actions().isEmpty()) menu->actions().front()->trigger();
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) menu->close();
        });
        emit modelTree->customContextMenuRequested(modelTree->visualItemRect(item).center());
        application.processEvents();
    };
    group(QStringLiteral("Sources"))->setExpanded(true);
    rightClick(group(QStringLiteral("Loads")));
    rightClick(group(QStringLiteral("Sources"))->child(0));
    rightClick(group(QStringLiteral("Deck")));
    group(QStringLiteral("Sources"))->setExpanded(false);
    const auto treeAdds = addedTo == std::vector{ModelNode::Loads, ModelNode::Sources};
    if (!modelTreeSummaries || !treeAdds) {
        QStringList shown;
        for (auto index = 0; index < modelTree->topLevelItemCount(); ++index)
            shown << modelTree->topLevelItem(index)->text(0) + QLatin1Char('=')
                    + modelTree->topLevelItem(index)->text(1) + QLatin1Char('/')
                    + modelTree->topLevelItem(index)->toolTip(0);
        qWarning() << "model tree" << shown << addedTo.size();
    }
    // The setup rows show their values, the detail in tooltips.
    const auto setupRowsShown = summary(QStringLiteral("Frequency")) == QStringLiteral("14–14.2 MHz · 3 pts")
        && summary(QStringLiteral("Ground")) == QStringLiteral("Free space")
        && summary(QStringLiteral("Requests")) == QStringLiteral("Pattern 19×37")
        && summary(QStringLiteral("Z0")) == QStringLiteral("50 Ω")
        && group(QStringLiteral("Z0"))->toolTip(0) == QStringLiteral("Reference impedance for SWR: the default 50 Ω")
        && group(QStringLiteral("Ground"))->foreground(0).color() != dim;
    modelTree->setStudyStatus(Study::AverageGain, StudyState::Complete,
        QStringLiteral("1.000 @ 14.100 MHz"));
    modelTree->setStudyStatus(Study::Convergence, StudyState::Stale);
    const auto* averageGainState = modelTree->findChild<QLabel*>(
        QStringLiteral("modelTreeAverageGainState"));
    const auto* convergenceState = modelTree->findChild<QLabel*>(
        QStringLiteral("modelTreeConvergenceState"));
    // Only a finished study can go stale.
    const auto studiesShown = averageGainState->text() == QStringLiteral("1.000 @ 14.100 MHz")
        && convergenceState->text() == QStringLiteral("not run");
    const auto widthBeforeLongStudy = modelTree->minimumSizeHint().width();
    const auto longStudy = QStringLiteral(
        "Not yet stable across two consecutive refinements. Review the trend or add levels.");
    modelTree->setStudyStatus(Study::Convergence, StudyState::Complete, longStudy);
    application.processEvents();
    const auto longStudyKeepsWidth = modelTree->minimumSizeHint().width() == widthBeforeLongStudy
        && convergenceState->toolTip() == longStudy;
    modelTree->clearStudy(Study::AverageGain);
    const auto studyCleared = averageGainState->text() == QStringLiteral("not run");
    for (const auto* name : {"modelTreeAverageGainRun", "modelTreeConvergenceRun"}) {
        if (auto* run = modelTree->findChild<QToolButton*>(QString::fromLatin1(name))) run->click();
    }
    // Clicking a study's result line opens it, like clicking its name.
    activatedSelection.reset();
    modelTree->scrollToItem(group(QStringLiteral("Validation"))->child(1));
    application.processEvents();
    QTest::mouseClick(const_cast<QLabel*>(convergenceState), Qt::LeftButton);
    application.processEvents();
    const auto studyRunButtons = treeAverageGainTriggered && treeConvergenceTriggered
        && !menuTriggered && activatedSelection
        && activatedSelection->node == ModelNode::Convergence;
    if (!studyRunButtons)
        qWarning() << "tree studies" << treeAverageGainTriggered << treeConvergenceTriggered
                   << menuTriggered << activatedSelection.has_value();
    // A row picked by the user is reported; one picked by the window is not.
    const auto fittedHeight = modelTree->height();
    group(QStringLiteral("Wires"))->setExpanded(true);
    application.processEvents();
    // Whole rows, no more than it was fitted to, and no taller once a group opens.
    const auto rowPixels = modelTree->heightForRows(1) - modelTree->heightForRows(0);
    const auto treeKeepsHeight = fittedHeight > 0 && rowPixels > 0
        && (fittedHeight - modelTree->heightForRows(0)) % rowPixels == 0
        && fittedHeight <= modelTree->heightForRows(modelTree->fittedRows())
        && modelTree->height() == fittedHeight;
    modelTree->setCurrentItem(group(QStringLiteral("Wires"))->child(0));
    const auto wireReported = !reportedSelections.empty()
        && reportedSelections.back() == ModelSelection{ModelNode::Wire, 1};
    allowSelectionChange = false;
    modelTree->setCurrentItem(group(QStringLiteral("Deck")));
    const auto selectionHeld = modelTree->currentSelection()
        && *modelTree->currentSelection() == ModelSelection{ModelNode::Wire, 1};
    allowSelectionChange = true;
    const auto reportedBefore = reportedSelections.size();
    modelTree->select({ModelNode::Source, -1, 3});
    const auto windowSelectionQuiet = reportedSelections.size() == reportedBefore
        && modelTree->currentSelection()
        && *modelTree->currentSelection() == ModelSelection{ModelNode::Source, -1, 3};
    modelTree->setModel(antenna, setup, modelTreeDocument);
    const auto selectionSurvivesRebuild = modelTree->currentSelection()
        && *modelTree->currentSelection() == ModelSelection{ModelNode::Source, -1, 3};
    modelTree->setCurrentItem(group(QStringLiteral("Deck")));
    emit modelTree->itemClicked(group(QStringLiteral("Deck")), 0);
    const auto deckActivated = activatedSelection
        && activatedSelection->node == ModelNode::Deck;
    QImage modelTreeImage(460, 700, QImage::Format_ARGB32_Premultiplied);
    modelTreeImage.fill(Qt::transparent); QPainter modelTreePainter(&modelTreeImage);
    modelColumn.render(&modelTreePainter); modelTreePainter.end();
    const auto modelTreeStateVisible = modelTreeGroups && modelTreeSummaries && studiesShown
        && studyCleared && studyRunButtons && treeAdds && setupRowsShown;
    // The properties pane: leaving applies a valid edit, an invalid one asks first, Enter
    // applies and Esc reverts. A page marks its unapplied edits on its Apply button.
    auto* editPage = new QWidget;
    auto* editValue = new QLineEdit(QStringLiteral("1"), editPage);
    auto* editApply = new QPushButton(QStringLiteral("Apply"), editPage);
    auto* editLayout = new QVBoxLayout(editPage);
    editLayout->addWidget(editValue);
    editLayout->addWidget(editApply);
    auto applications = 0;
    QString appliedValue = editValue->text();
    QObject::connect(editApply, &QPushButton::clicked, editApply, [&] {
        bool number{};
        static_cast<void>(editValue->text().toDouble(&number));
        if (!number) return;
        ++applications;
        appliedValue = editValue->text();
        necwb::ui::setPendingEditIndicator(editApply, false);
    });
    propertiesPane->setDiscardCallback([&](QWidget* page) {
        if (page != editPage) return;
        editValue->setText(appliedValue);
        necwb::ui::setPendingEditIndicator(editApply, false);
    });
    const auto editTo = [&](const QString& text) {
        editValue->setText(text);
        necwb::ui::setPendingEditIndicator(editApply, true);
    };
    modelColumn.showPage(editPage, QStringLiteral("Page"));
    application.processEvents();
    editTo(QStringLiteral("0.75"));
    const auto validEditApplied = propertiesPane->commitPendingEdits() && applications == 1
        && appliedValue == QStringLiteral("0.75");
    const auto answerInvalidEdit = [](QMessageBox::StandardButton answer) {
        QTimer::singleShot(0, [answer] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(answer)->click();
        });
    };
    editTo(QStringLiteral("x"));
    answerInvalidEdit(QMessageBox::Cancel);
    const auto invalidEditHeld = !propertiesPane->commitPendingEdits()
        && editApply->property("pendingChanges").toBool() && applications == 1;
    answerInvalidEdit(QMessageBox::Discard);
    const auto invalidEditDiscarded = propertiesPane->commitPendingEdits()
        && editValue->text() == QStringLiteral("0.75") && applications == 1;
    editTo(QStringLiteral("0.6"));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(propertiesPane, &enter);
    const auto enterApplies = applications == 2 && appliedValue == QStringLiteral("0.6");
    editTo(QStringLiteral("0.9"));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(propertiesPane, &escape);
    const auto escapeReverts = applications == 2 && editValue->text() == QStringLiteral("0.6");
    modelColumn.showPage(nullptr, {});
    application.processEvents();
    const auto paneHides = propertiesPane->isHidden() && propertiesPane->currentPage() == nullptr;
    // With the pane empty the variables take the free height: every row, no scroll bar.
    std::vector<necwb::nec::SymbolDefinition> manyVariables;
    for (auto index = 1; index <= 8; ++index)
        manyVariables.push_back({"v" + std::to_string(index), "1", 1.0, static_cast<std::size_t>(index)});
    auto* columnVariables = modelColumn.variables();
    columnVariables->setVariables(manyVariables);
    application.processEvents();
    const auto* variablesScroll = columnVariables->findChild<QScrollArea*>();
    const auto allRowsShown = [&] {
        return variablesScroll != nullptr && variablesScroll->verticalScrollBar()->maximum() == 0
            && columnVariables->height() >= columnVariables->unfoldedHeight();
    };
    const auto rowsWithPaneEmpty = allRowsShown();
    // So they do beside a short page, which needs only its own height.
    modelColumn.showPage(editPage, QStringLiteral("Page"));
    application.processEvents();
    const auto variablesTakeFreeHeight = rowsWithPaneEmpty && allRowsShown()
        && !propertiesPane->isHidden();
    modelColumn.showPage(nullptr, {});
    if (!variablesTakeFreeHeight)
        qWarning() << "variables with the pane empty" << rowsWithPaneEmpty << columnVariables->height()
                   << columnVariables->unfoldedHeight() << columnVariables->sizeHint()
                   << columnVariables->maximumHeight() << modelColumn.height()
                   << modelTree->height();
    columnVariables->setVariables({});
    // A cell edit changes only its own field: the others keep their exact values, even in a
    // deck unit that does not round-trip, such as inches.
    necwb::model::AntennaModel preciseModel;
    preciseModel.addWire({4, {0.1234567890123457, 0.0, -0.5}, {0.1, 0.0, 0.5}, 11, 0.00123456, 1});
    necwb::ui::WireCardEditor preciseTable;
    preciseTable.setDeckScale(0.0254, QStringLiteral("in"));
    preciseTable.setModel(preciseModel);
    std::optional<necwb::model::Wire> preciseApplied;
    QObject::connect(&preciseTable, &necwb::ui::WireCardEditor::wireEdited,
        [&preciseApplied](const auto&, const necwb::model::Wire& updated) { preciseApplied = updated; });
    if (auto* cells = preciseTable.findChild<QTableWidget*>(QStringLiteral("wireCardTable")))
        cells->item(0, 7)->setText(QStringLiteral("20"));
    application.processEvents();
    const auto& preciseWire = preciseModel.wires().front();
    const auto untouchedFieldsExact = preciseApplied && preciseApplied->start == preciseWire.start
        && preciseApplied->end.x == preciseWire.end.x && preciseApplied->end.y == preciseWire.end.y
        && preciseApplied->radius == preciseWire.radius && preciseApplied->segments == 11
        && std::abs(preciseApplied->end.z - 20 * 0.0254) < 1.0e-12;
    // The variables: a slider for each plain number and the value of each formula. A drag,
    // a typed value or a burst of steps is one change.
    necwb::ui::VariablesSection variablesSection;
    std::vector<std::pair<std::string, double>> variableChanges;
    std::vector<std::pair<QString, necwb::ui::VariablesSection::Range>> rangeChanges;
    std::optional<necwb::ui::VariablesSection::Range> optimizerRange;
    variablesSection.setValueChangedCallback([&](const necwb::nec::SymbolDefinition& variable,
                                                 double value) {
        variableChanges.emplace_back(variable.name, value);
    });
    variablesSection.setRangeChangedCallback([&](const QString& name, const auto& range) {
        rangeChanges.emplace_back(name, range);
    });
    variablesSection.setRangeProvider([&](const QString& name) {
        return name == QStringLiteral("len") ? optimizerRange : std::nullopt;
    });
    variablesSection.setVariables({{"len", "0.5", 0.5, 1}, {"twice", "len*2", 1.0, 1},
        {"offset", "0", 0.0, 2}});
    variablesSection.resize(460, 200);
    variablesSection.show();
    application.processEvents();
    const auto variableSliders = variablesSection.findChildren<QSlider*>(
        QStringLiteral("variableSlider"));
    const auto variableBoxes = variablesSection.findChildren<QDoubleSpinBox*>(
        QStringLiteral("variableValue"));
    const auto* formula = variablesSection.findChild<QLabel*>(QStringLiteral("variableFormula"));
    const auto variablesShown = variableSliders.size() == 2 && variableBoxes.size() == 2
        && formula != nullptr && formula->text() == QStringLiteral("= len*2 → 1.000")
        // ±25% around 0.5, and -1 to 1 around zero: both start in the middle.
        && variableSliders[0]->value() == 500 && variableSliders[1]->value() == 500;
    auto* lengthSlider = variableSliders.value(0);
    auto* lengthBox = variableBoxes.value(0);
    if (lengthSlider == nullptr || lengthBox == nullptr)
        return NECWB_SMOKE_FAILURE("variable controls");
    lengthSlider->setSliderDown(true);
    lengthSlider->setValue(600);
    lengthSlider->setValue(800);
    application.processEvents();
    const auto noChangeDuringDrag = variableChanges.empty()
        && std::abs(lengthBox->value() - 0.575) < 1.0e-12;
    lengthSlider->setSliderDown(false);
    application.processEvents();
    const auto dragIsOneChange = variableChanges.size() == 1
        && variableChanges.back() == std::pair{std::string{"len"}, 0.575};
    lengthBox->setValue(0.55);
    emit lengthBox->editingFinished();
    application.processEvents();
    const auto typedValueChanges = variableChanges.size() == 2
        && variableChanges.back().second == 0.55;
    lengthSlider->setValue(510);
    lengthSlider->setValue(520);
    application.processEvents();
    const auto stepsWait = variableChanges.size() == 2;
    QEventLoop stepPause;
    QTimer::singleShot(600, &stepPause, &QEventLoop::quit);
    stepPause.exec();
    const auto stepsAreOneChange = variableChanges.size() == 3
        && std::abs(variableChanges.back().second - 0.505) < 1.0e-12;
    optimizerRange = necwb::ui::VariablesSection::Range{0.4, 0.9};
    variablesSection.refreshRanges();
    const auto optimizerRangeUsed = lengthSlider->value() == 200;
    QTimer::singleShot(0, [] {
        auto* popup = QApplication::activePopupWidget();
        if (popup == nullptr) return;
        auto* minimum = popup->findChild<QDoubleSpinBox*>(QStringLiteral("variableRangeMinimum"));
        auto* maximum = popup->findChild<QDoubleSpinBox*>(QStringLiteral("variableRangeMaximum"));
        auto* set = popup->findChild<QPushButton*>(QStringLiteral("variableRangeSet"));
        if (minimum == nullptr || maximum == nullptr || set == nullptr) return;
        minimum->setValue(-2.0);
        maximum->setValue(2.0);
        set->click();
    });
    if (auto* rangeButton = variablesSection.findChildren<QToolButton*>(
            QStringLiteral("variableRange")).value(1))
        rangeButton->click();
    const auto rangeEditReported = rangeChanges.size() == 1
        && rangeChanges.front().first == QStringLiteral("offset")
        && rangeChanges.front().second == necwb::ui::VariablesSection::Range{-2.0, 2.0}
        && variableSliders[1]->value() == 500;
    // Reset: saving keeps the opened values, and a variable added since keeps its first value.
    variablesSection.markBaseline();
    variablesSection.setVariables({{"len", "0.5", 0.5, 1}, {"twice", "len*2", 1.0, 1},
        {"offset", "0", 0.0, 2}, {"w", "1", 1.0, 1}});
    variablesSection.setVariables({{"len", "0.6", 0.6, 1}, {"twice", "len*2", 1.2, 1},
        {"offset", "0", 0.0, 2}, {"w", "2", 2.0, 1}});
    const auto* variablesReset = variablesSection.findChild<QToolButton*>(
        QStringLiteral("variablesReset"));
    const auto resetKeepsAdded = variablesReset != nullptr && variablesReset->isEnabled()
        && variablesReset->toolTip().contains(QStringLiteral("len 0.6 → 0.5"))
        && variablesReset->toolTip().contains(QStringLiteral("w 2 → 1"));
    // The toolbar: one row with the engine; Configure… is a command, not an engine.
    QMenu recentModels;
    QAction toolOpen(QStringLiteral("&Open…")), toolSave(QStringLiteral("&Save")),
        toolRun(QStringLiteral("&Run Analysis")), toolStop(QStringLiteral("S&top")),
        toolSweep(QStringLiteral("Quick Frequency &Sweep…")), toolCheck(QStringLiteral("&Check Model")),
        toolOptimize(QStringLiteral("&Optimize"));
    necwb::ui::WorkbenchToolBar toolBar({.open = &toolOpen, .recentModels = &recentModels,
        .save = &toolSave, .run = &toolRun, .stop = &toolStop, .sweep = &toolSweep,
        .check = &toolCheck, .optimize = &toolOptimize});
    QString selectedBackend;
    auto configureRequested = false;
    toolBar.setBackendSelectedCallback([&](const QString& id) { selectedBackend = id; });
    toolBar.setConfigureCallback([&] { configureRequested = true; });
    // Only the engine in use is named for where it was found.
    toolBar.setBackend(QStringLiteral("nec2"), QStringLiteral("nec2c (bundled)"),
        QStringLiteral("nec2c 1.3.3 (55be1e0) · bundled · /opt/nec2c"));
    auto* backend = toolBar.findChild<QComboBox*>(QStringLiteral("toolBarBackend"));
    const auto bundledNamed = backend != nullptr
        && backend->itemText(backend->findData(QStringLiteral("nec2"))) == QStringLiteral("nec2c (bundled)")
        && backend->toolTip().contains(QStringLiteral("1.3.3 (55be1e0) · bundled"));
    toolBar.setBackend(QStringLiteral("opennec"), QStringLiteral("OpenNEC"), QStringLiteral("OpenNEC · not found"));
    auto* toolOpenButton = toolBar.findChild<QToolButton*>(QStringLiteral("toolBarOpen"));
    const auto backendShownQuietly = bundledNamed
        && backend->currentData().toString() == QStringLiteral("opennec") && selectedBackend.isEmpty()
        && backend->itemText(backend->findData(QStringLiteral("nec2"))) == QStringLiteral("NEC-2 / nec2c")
        && backend->toolTip().contains(QStringLiteral("not found"));
    if (backend != nullptr) {
        emit backend->activated(backend->findData(QStringLiteral("nec2dxs")));
        emit backend->activated(backend->count() - 1);
    }
    const auto toolBarValid = backendShownQuietly && selectedBackend == QStringLiteral("nec2dxs")
        && configureRequested && backend->currentData().toString() == QStringLiteral("nec2dxs")
        && toolOpenButton != nullptr && toolOpenButton->menu() == &recentModels
        && toolRun.iconText() == QStringLiteral("Run") && toolRun.text() == QStringLiteral("&Run Analysis");
    const std::array modelTreeChecks{std::pair{"groups", modelTreeGroups},
        std::pair{"summaries", modelTreeSummaries}, std::pair{"studies", studiesShown},
        std::pair{"long study keeps width", longStudyKeepsWidth},
        std::pair{"study cleared", studyCleared}, std::pair{"study run buttons", studyRunButtons},
        std::pair{"tree keeps height", treeKeepsHeight}, std::pair{"wire reported", wireReported},
        std::pair{"selection held", selectionHeld},
        std::pair{"window selection quiet", windowSelectionQuiet},
        std::pair{"selection survives rebuild", selectionSurvivesRebuild},
        std::pair{"deck activated", deckActivated}, std::pair{"tree adds", treeAdds},
        std::pair{"setup rows", setupRowsShown},
        std::pair{"valid edit applied", validEditApplied},
        std::pair{"invalid edit held", invalidEditHeld},
        std::pair{"invalid edit discarded", invalidEditDiscarded},
        std::pair{"enter applies", enterApplies}, std::pair{"escape reverts", escapeReverts},
        std::pair{"pane hides", paneHides}, std::pair{"variables shown", variablesShown},
        std::pair{"variables take the free height", variablesTakeFreeHeight},
        std::pair{"no change during drag", noChangeDuringDrag},
        std::pair{"drag is one change", dragIsOneChange},
        std::pair{"typed value changes", typedValueChanges}, std::pair{"steps wait", stepsWait},
        std::pair{"steps are one change", stepsAreOneChange},
        std::pair{"optimizer range used", optimizerRangeUsed},
        std::pair{"range edit reported", rangeEditReported},
        std::pair{"reset keeps added variables", resetKeepsAdded},
        std::pair{"untouched fields exact", untouchedFieldsExact},
        std::pair{"toolbar", toolBarValid}};
    for (const auto& [name, passed] : modelTreeChecks) {
        if (passed) continue;
        std::fprintf(stderr, "model tree %s failed: groups \"%s\", wires \"%s\"\n", name,
            qUtf8Printable(groupNames.join(QStringLiteral(", "))),
            qUtf8Printable(summary(QStringLiteral("Wires"))));
        return NECWB_SMOKE_FAILURE("model tree");
    }
    // The results column: one selected frequency drives the key numbers, the thumbnails
    // and the full view, and survives a new run that has it.
    NECWB_SMOKE_STEP();
    {
        QSettings{}.remove(QStringLiteral("resultsColumn"));
        QWidget host;
        necwb::ui::ResultsController results(&host);
        auto fullViewShown = false;
        std::optional<double> statusFrequency;
        results.setShowFullViewCallback([&fullViewShown](bool shown) { fullViewShown = shown; });
        results.setFrequenciesChangedCallback(
            [&statusFrequency](const auto&, std::optional<double> selected) {
                statusFrequency = selected;
            });
        auto* column = results.column();
        auto* fullView = results.fullView();
        auto* hostLayout = new QHBoxLayout(&host);
        hostLayout->addWidget(fullView, 1);
        hostLayout->addWidget(column);
        host.resize(1100, 900);
        host.show();
        application.processEvents();
        const auto text = [column](const char* name) {
            const auto* found = column->findChild<QLabel*>(QString::fromLatin1(name));
            return found == nullptr ? QString{} : found->text();
        };
        auto* frequency = column->findChild<QComboBox*>(QStringLiteral("resultsFrequency"));
        auto* empty = column->findChild<QLabel*>(QStringLiteral("resultsColumnEmpty"));
        auto* azimuth = column->findChild<QWidget*>(QStringLiteral("azimuthThumbnail"));
        auto* currents = column->findChild<QWidget*>(QStringLiteral("currentsThumbnail"));
        if (frequency == nullptr || empty == nullptr || azimuth == nullptr || currents == nullptr)
            return NECWB_SMOKE_FAILURE("results column controls");
        // With no results nothing stretches: the strip keeps its own height, the overlay row
        // is hidden, and the empty line sits right under the strip.
        const auto* strip = column->findChild<QWidget*>(QStringLiteral("resultsStrip"));
        const auto* overlayToggle =
            column->findChild<QWidget*>(QStringLiteral("showHistoricalOverlay"));
        const auto emptyState = empty->isVisible() && !azimuth->isVisible() && strip != nullptr
            && overlayToggle != nullptr && !overlayToggle->isVisible()
            && strip->height() <= strip->sizeHint().height()
            && empty->y() - (strip->y() + strip->height()) < 12
            && empty->height() <= empty->sizeHint().height();
        if (!emptyState) {
            std::fprintf(stderr, "empty results column: strip %d (hint %d), empty at %d h %d\n",
                strip == nullptr ? -1 : strip->height(),
                strip == nullptr ? -1 : strip->sizeHint().height(), empty->y(), empty->height());
        }
        results.setResults(necwb::ui::ResultData::make(result), QStringLiteral("column-run"), {});
        application.processEvents();
        // A first run selects minimum SWR: 50 Ω at 14.1 MHz.
        const auto minimumSwrSelected = frequency->count() == 3
            && frequency->currentData().toDouble() == 14.1 && statusFrequency == 14.1
            && !empty->isVisible() && azimuth->isVisible() && overlayToggle->isVisible()
            && azimuth->height() > 100
            && text("resultsSwr").startsWith(QStringLiteral("1"))
            && text("resultsSwr").contains(QStringLiteral("(Z0 50 Ω)"))
            && text("resultsGain").contains(QStringLiteral("dBi @"))
            && text("resultsExtrema").contains(QStringLiteral("Min SWR"))
            && text("resultsExtrema").contains(QStringLiteral("Band peak"));
        // 14.0 MHz has impedance only: the pattern and current thumbnails shrink to a note.
        frequency->setCurrentIndex(frequency->findData(14.0));
        application.processEvents();
        const auto missingDataNoted = statusFrequency == 14.0
            && text("resultsGain") == QString::fromUtf8("—")
            && azimuth->height() < 60 && currents->height() < 60;
        frequency->setCurrentIndex(frequency->findData(14.2));
        results.setResults(necwb::ui::ResultData::make(result), QStringLiteral("rerun"), {});
        application.processEvents();
        const auto frequencyKept = frequency->currentData().toDouble() == 14.2;
        results.setReferenceImpedance(200.0);
        application.processEvents();
        const auto z0Followed = frequency->currentData().toDouble() == 14.2
            && text("resultsSwr").contains(QStringLiteral("(Z0 200 Ω)"));
        results.setStale(true);
        application.processEvents();
        const auto* stale = column->findChild<QLabel*>(QStringLiteral("resultsStale"));
        const auto staleMarked = stale != nullptr && stale->isVisible()
            && !column->findChild<QWidget*>(QStringLiteral("resultsStripValues"))->isEnabled();
        results.setStale(false);
        // Clicking a thumbnail opens its view in the center; Esc goes back.
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(20, 20), azimuth->mapToGlobal(QPointF(20, 20)),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(20, 20),
            azimuth->mapToGlobal(QPointF(20, 20)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(azimuth, &press);
        QApplication::sendEvent(azimuth, &release);
        application.processEvents();
        const auto* title = fullView->findChild<QLabel*>(QStringLiteral("resultFullViewTitle"));
        const auto fullViewOpened = fullViewShown && title != nullptr
            && title->text() == QStringLiteral("Azimuth pattern")
            && fullView->findChild<QComboBox*>(QStringLiteral("radiation2DFrequency"))->isHidden();
        for (auto* escapeShortcut : fullView->findChildren<QShortcut*>()) {
            if (escapeShortcut->key() == QKeySequence(Qt::Key_Escape))
                emit escapeShortcut->activated();
        }
        const auto escapeReturned = !fullViewShown;
        // A row clicked in the full impedance table selects its frequency everywhere.
        results.showView(necwb::ui::ResultView::ImpedanceTable);
        application.processEvents();
        // The impedance table has nine columns; the currents table eight.
        const auto tables = fullView->findChildren<QTableWidget*>();
        const auto impedanceTable = std::ranges::find_if(tables,
            [](const QTableWidget* table) { return table->columnCount() == 9; });
        if (impedanceTable != tables.end()) {
            QMetaObject::invokeMethod(*impedanceTable, "cellClicked", Qt::DirectConnection,
                Q_ARG(int, 0), Q_ARG(int, 0));
        }
        application.processEvents();
        const auto tableSetsFrequency = frequency->currentData().toDouble() == 14.0
            && statusFrequency == 14.0;
        // The currents thumbnail never animates.
        const auto noAnimation = std::ranges::none_of(column->findChildren<QTimer*>(),
            [](const QTimer* timer) { return timer->isActive(); });
        results.clear();
        results.setResults(necwb::ui::ResultData::make(result), QStringLiteral("next-model"), {});
        const auto clearForgetsFrequency = frequency->currentData().toDouble() == 14.1;
        // A live point shows in the column only: data(), the full view and its exports keep
        // the full run, and only the live frequency is up to date.
        application.processEvents();
        const auto fullData = results.data();
        results.setStale(true);
        necwb::analysis::AnalysisResult point;
        point.feedpoints = {{14.1, 1, 6, {1.0, 0.0}, {0.01, 0.0}, {100.0, 0.0}, 0.005}};
        auto exported = false;
        results.setStatusCallback([&exported](const QString& message) {
            exported = message.startsWith(QStringLiteral("Exported the last full run"));
        });
        results.setLivePoint(point, 14.1);
        results.showView(necwb::ui::ResultView::Azimuth);
        application.processEvents();
        QTimer::singleShot(0, [] {
            if (auto* dialog = QApplication::activeModalWidget()) dialog->close();
        });
        if (auto* exportImage = fullView->findChild<QPushButton*>(
                QStringLiteral("radiation2DExportImage")))
            exportImage->click();
        const auto liveDisplayOnly = results.data() == fullData
            && text("resultsSwr").startsWith(QStringLiteral("2")) && !stale->isVisible()
            && !results.isStaleAt(14.1) && results.isStaleAt(14.0) && exported
            && title->text().contains(QStringLiteral("last full run"));
        results.clearLivePoints();
        const auto liveCleared = text("resultsSwr").startsWith(QStringLiteral("1"))
            && stale->isVisible();
        const std::array checks{std::pair{"empty state", emptyState},
            std::pair{"minimum SWR selected", minimumSwrSelected},
            std::pair{"missing data noted", missingDataNoted},
            std::pair{"frequency kept", frequencyKept}, std::pair{"Z0 followed", z0Followed},
            std::pair{"stale marked", staleMarked}, std::pair{"full view opened", fullViewOpened},
            std::pair{"escape returned", escapeReturned},
            std::pair{"table sets frequency", tableSetsFrequency},
            std::pair{"no animation", noAnimation},
            std::pair{"clear forgets frequency", clearForgetsFrequency},
            std::pair{"live display only", liveDisplayOnly},
            std::pair{"live cleared", liveCleared}};
        for (const auto& [name, passed] : checks) {
            if (passed) continue;
            std::fprintf(stderr, "results column %s failed: frequency %s, SWR \"%s\", gain \"%s\"\n",
                name, qUtf8Printable(frequency->currentText()), qUtf8Printable(text("resultsSwr")),
                qUtf8Printable(text("resultsGain")));
            return NECWB_SMOKE_FAILURE("results column");
        }
        QSettings{}.remove(QStringLiteral("resultsColumn"));
    }
    // The start panel: its buttons trigger New, Open and Open Example, its recent list shows
    // file, folder and when each was opened, opens a row when activated, and is no taller
    // than its rows.
    auto startPanelValid = false;
    NECWB_SMOKE_STEP();
    {
        QTemporaryDir recentDirectory;
        const auto firstPath = recentDirectory.filePath(QStringLiteral("first.nec"));
        const auto secondPath = recentDirectory.filePath(QStringLiteral("second.nec"));
        for (const auto& path : {firstPath, secondPath}) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) file.write("GE 0\nEN\n");
        }
        QSettings recentSettings;
        const auto savedList = recentSettings.value(QStringLiteral("files/recentModels"));
        const auto savedOpened = recentSettings.value(QStringLiteral("files/recentOpened"));
        necwb::ui::recentModels::clear();
        necwb::ui::recentModels::remember(secondPath);
        const auto files = necwb::ui::recentModels::remember(firstPath);
        QAction startNew;
        QAction startOpen;
        auto newTriggered = false;
        auto openTriggered = false;
        auto exampleRequested = false;
        QObject::connect(&startNew, &QAction::triggered, [&newTriggered] { newTriggered = true; });
        QObject::connect(&startOpen, &QAction::triggered, [&openTriggered] { openTriggered = true; });
        QString opened;
        necwb::ui::StartPanel start(&startNew, &startOpen,
            [&exampleRequested] { exampleRequested = true; },
            [&opened](const QString& path) { opened = path; });
        start.resize(900, 650);
        start.setRecentFiles(files);
        start.show();
        application.processEvents();
        auto* list = start.findChild<QTreeWidget*>(QStringLiteral("startRecentModels"));
        for (const auto* name : {"startNewModel", "startOpenModel", "startOpenExample"})
            if (auto* button = start.findChild<QPushButton*>(QString::fromLatin1(name))) button->click();
        if (list != nullptr && list->topLevelItemCount() == 2)
            emit list->itemActivated(list->topLevelItem(1), 0);
        startPanelValid = list != nullptr && list->topLevelItemCount() == 2
            && list->topLevelItem(0)->text(0) == QStringLiteral("first.nec")
            && list->topLevelItem(0)->text(1)
                == QDir::toNativeSeparators(QFileInfo(firstPath).absolutePath())
            && !list->topLevelItem(0)->text(2).isEmpty()
            && opened == QFileInfo(secondPath).absoluteFilePath()
            && newTriggered && openTriggered && exampleRequested
            && list->height() < 4 * list->sizeHintForRow(0) + list->header()->height() + 8
            && list->geometry().bottom() < start.height() / 2;
        if (!startPanelValid)
            qWarning() << "start panel" << (list ? list->topLevelItemCount() : -1) << opened
                       << newTriggered << openTriggered << exampleRequested
                       << (list ? list->height() : -1);
        for (const auto& [key, value] : {std::pair{QStringLiteral("files/recentModels"), savedList},
                 std::pair{QStringLiteral("files/recentOpened"), savedOpened}}) {
            if (value.isValid()) recentSettings.setValue(key, value);
            else recentSettings.remove(key);
        }
    }
    necwb::ui::StructuredCardEditor structuredCards({{}, true});
    const auto structuredCardsCompact = hasCompactMargins(structuredCards, 6);
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
    auto* structuredFamilies = structuredCards.findChild<QTreeWidget*>(
        QStringLiteral("structuredCardFamilies"));
    auto* structuredAdd = structuredCards.findChild<QPushButton*>(
        QStringLiteral("structuredAddCardButton"));
    const auto selectStructuredFamily = [](QTreeWidget* tree, const QString& prefix) {
        QTreeWidgetItemIterator item(tree);
        while (*item != nullptr) {
            if ((*item)->childCount() == 0 && (*item)->text(0).startsWith(prefix)) {
                tree->setCurrentItem(*item);
                return true;
            }
            ++item;
        }
        return false;
    };
    if (structuredTable == nullptr || structuredFamilies == nullptr || structuredAdd == nullptr
        || !selectStructuredFamily(structuredFamilies, QStringLiteral("EX")))
        return NECWB_SMOKE_FAILURE("structured card controls");
    application.processEvents();
    const auto structuredHierarchyValid = structuredFamilies->topLevelItemCount() == 6
        && structuredFamilies->topLevelItem(0)->text(0) == QStringLiteral("Geometry")
        && structuredFamilies->topLevelItem(3)->text(0) == QStringLiteral("Loads & Networks")
        && structuredFamilies->topLevelItem(4)->childCount() == 4;
    if (structuredTable->rowCount() != 1 || !structuredAdd->isEnabled())
        return NECWB_SMOKE_FAILURE("structured card family selection");
    const auto structuredSelectionWorks = structuredCards.selectCard(3)
        && structuredFamilies->currentItem()->text(0).startsWith(QStringLiteral("FR"))
        && structuredTable->currentRow() == 0;
    application.processEvents();
    const auto duplicateFrequencyBlocked = !structuredAdd->isEnabled();
    selectStructuredFamily(structuredFamilies, QStringLiteral("EX"));
    application.processEvents();
    // A segment takes a name, but not a number that is not whole.
    structuredTable->item(0, 4)->setText(QStringLiteral("2.5"));
    application.processEvents();
    const auto invalidEditBlocked = editedCard.isEmpty()
        && structuredTable->item(0, 4)->toolTip() == QStringLiteral("Enter a whole number.");
    structuredTable->item(0, 4)->setText(QStringLiteral("7"));
    structuredCards.show(); application.processEvents();
    structuredTable->item(0, 8)->setText(QStringLiteral("2.5"));
    application.processEvents();
    // A number as the parser reads it, "+1" too, goes into the deck; anything else does not.
    const auto tableAccepts = [&](const QString& text) {
        editedCard.clear();
        structuredTable->item(0, 8)->setText(text);
        application.processEvents();
        return editedCard.contains(QLatin1Char(' ') + text);
    };
    const auto tableNumbers = tableAccepts(QStringLiteral("+1")) && tableAccepts(QStringLiteral("1e-3"))
        && tableAccepts(QStringLiteral("-0.5")) && !tableAccepts(QStringLiteral("abc"))
        && !tableAccepts(QStringLiteral("1..2"));
    tableAccepts(QStringLiteral("2.5"));
    // An NT with zero admittances would open the wire at its ports, so Add NT writes a 50 Ω
    // series connection.
    selectStructuredFamily(structuredFamilies, QStringLiteral("NT"));
    application.processEvents();
    structuredAdd->click();
    application.processEvents();
    if (!structuredAdd->isEnabled() || addedCard != QStringLiteral("NT 1 6 1 6 0.02 0 -0.02 0 0.02 0"))
        return NECWB_SMOKE_FAILURE("Add NT writes a 50 ohm series network");
    selectStructuredFamily(structuredFamilies, QStringLiteral("LD"));
    application.processEvents();
    structuredAdd->click();
    application.processEvents();
    selectStructuredFamily(structuredFamilies, QStringLiteral("EX"));
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
    necwb::ui::StructuredCardEditor extendedCards({{}, true});
    extendedCards.setDocument(necwb::nec::NecParser{}.parse(
        "FR 0 101 0 0 3.0 0.05 8.0 0 0 0\n"));
    auto* extendedFamilies = extendedCards.findChild<QTreeWidget*>(
        QStringLiteral("structuredCardFamilies"));
    auto* extendedTable = extendedCards.findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
    QString extendedFrequency;
    QObject::connect(&extendedCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&extendedFrequency](std::size_t, const QString& text) { extendedFrequency = text; });
    selectStructuredFamily(extendedFamilies, QStringLiteral("FR"));
    application.processEvents();
    extendedTable->item(0, 6)->setText(QStringLiteral("3.1"));
    application.processEvents();
    const auto trailingFieldsPreserved = extendedFrequency
        == QStringLiteral("FR 0 101 0 0 3.1 0.05 8.0 0 0 0");
    necwb::ui::StructuredCardEditor generatedGeometryCards({{}, true});
    generatedGeometryCards.setDeckUnitLabel(QStringLiteral("m"));
    generatedGeometryCards.setDocument(necwb::nec::NecParser{}.parse(
        "GA 1 9 0.5 0 180 0.001\n"
        "GH 2 80 0.03 0.30 0.02 0.02 0.02 0.02 0.001\nGE 0\n"));
    auto* generatedGeometryFamilies = generatedGeometryCards.findChild<QTreeWidget*>(
        QStringLiteral("structuredCardFamilies"));
    auto* generatedGeometryTable = generatedGeometryCards.findChild<QTableWidget*>(
        QStringLiteral("structuredCardTable"));
    auto* generatedGeometryAdd = generatedGeometryCards.findChild<QPushButton*>(
        QStringLiteral("structuredAddCardButton"));
    QString editedArc;
    QString addedGeneratedGeometry;
    QObject::connect(&generatedGeometryCards, &necwb::ui::StructuredCardEditor::cardEdited,
        [&editedArc](std::size_t, const QString& text) { editedArc = text; });
    QObject::connect(&generatedGeometryCards, &necwb::ui::StructuredCardEditor::cardAddRequested,
        [&addedGeneratedGeometry](const QString& text) { addedGeneratedGeometry = text; });
    const auto arcFamilyFound = selectStructuredFamily(
        generatedGeometryFamilies, QStringLiteral("GA"));
    application.processEvents();
    const auto arcFieldsLabeled = arcFamilyFound && generatedGeometryTable->rowCount() == 1
        && generatedGeometryTable->columnCount() == 8
        && generatedGeometryTable->horizontalHeaderItem(4)->text().contains(QStringLiteral("Arc Radius"))
        && generatedGeometryTable->horizontalHeaderItem(5)->text().contains(QStringLiteral("Start Angle"))
        && generatedGeometryTable->item(0, 4)->toolTip().contains(QStringLiteral("deck length"));
    generatedGeometryTable->item(0, 6)->setText(QStringLiteral("170"));
    application.processEvents();
    const auto arcEditCommitted = editedArc == QStringLiteral("GA 1 9 0.5 0 170 0.001");
    const auto helixFamilyFound = selectStructuredFamily(
        generatedGeometryFamilies, QStringLiteral("GH"));
    application.processEvents();
    const auto helixFieldsLabeled = helixFamilyFound && generatedGeometryTable->rowCount() == 1
        && generatedGeometryTable->columnCount() == 11
        && generatedGeometryTable->horizontalHeaderItem(4)->text() == QStringLiteral("Turn Spacing (m)")
        && generatedGeometryTable->horizontalHeaderItem(5)->text() == QStringLiteral("Axial Length (m)")
        && generatedGeometryTable->item(0, 5)->toolTip().contains(QStringLiteral("spiral"));
    generatedGeometryAdd->click();
    application.processEvents();
    const auto helixDefaultValid = addedGeneratedGeometry.startsWith(QStringLiteral("GH 3 "));
    selectStructuredFamily(generatedGeometryFamilies, QStringLiteral("Other NEC-2 Geometry"));
    application.processEvents();
    const auto generatedCardsExcludedFromOther = generatedGeometryTable->rowCount() == 0;
    NECWB_SMOKE_STEP();
    {
        // Long machine-written numbers display at 6 significant figures, but editing
        // still gets the exact deck text.
        using necwb::ui::compactNumberText;
        const auto rulesValid = compactNumberText(QStringLiteral("0.498372615524837"))
                == QStringLiteral("0.498373")
            && compactNumberText(QStringLiteral("-0.520")) == QStringLiteral("-0.520")
            && compactNumberText(QStringLiteral("0.00041")) == QStringLiteral("0.00041")
            && compactNumberText(QStringLiteral("space_driv*2")) == QStringLiteral("space_driv*2");
        necwb::ui::WireCardEditor compactWires;
        compactWires.resize(900, 300);
        necwb::model::AntennaModel compactModel;
        compactModel.addWire({1, {0.498372615524837, -0.5, 0.0},
            {0.498372615524837, 0.5, 0.0}, 11, 0.001, 1});
        compactWires.setModel(compactModel);
        compactWires.show();
        application.processEvents();
        auto* table = compactWires.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
        auto editsExactText = false;
        auto columnCompact = false;
        if (table != nullptr) {
            const auto index = table->model()->index(0, 2);
            auto* delegate = table->itemDelegateForIndex(index);
            auto* editor = delegate->createEditor(table->viewport(), QStyleOptionViewItem{}, index);
            delegate->setEditorData(editor, index);
            auto* line = qobject_cast<QLineEdit*>(editor);
            editsExactText = line != nullptr
                && line->text() == QStringLiteral("0.498372615524837");
            columnCompact = table->columnWidth(2)
                < table->fontMetrics().horizontalAdvance(QStringLiteral("0.498372615524837"));
            delete editor;
        }
        if (!rulesValid || !editsExactText || !columnCompact)
            return NECWB_SMOKE_FAILURE("compact card numbers");
    }
    necwb::ui::WireCardEditor wireEditor;
    wireEditor.resize(900, 400);
    necwb::model::AntennaModel wireModel;
    wireModel.addWire({1, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 11, 0.001, 1});
    wireEditor.setModel(wireModel);
    auto wireEditCommitted = false;
    auto structuredGaugeRadius = 0.0;
    QObject::connect(&wireEditor, &necwb::ui::WireCardEditor::wireEdited,
        [&wireEditor, &wireEditCommitted, &structuredGaugeRadius](
            const necwb::model::Wire&, const necwb::model::Wire& updated) {
            necwb::model::AntennaModel refreshed;
            refreshed.addWire(updated);
            wireEditor.setModel(refreshed);
            wireEditCommitted = true;
            structuredGaugeRadius = updated.radius;
        });
    auto* wireTable = wireEditor.findChild<QTableWidget*>(QStringLiteral("wireCardTable"));
    if (wireTable == nullptr || wireTable->rowCount() != 1)
        return NECWB_SMOKE_FAILURE("wire card rows");
    auto* structuredGauge = qobject_cast<QComboBox*>(wireTable->cellWidget(0, 9));
    if (structuredGauge == nullptr) return NECWB_SMOKE_FAILURE("wire gauge editor");
    // Custom, then AWG 0000 (-3) to 40.
    auto structuredGaugeRangeValid = structuredGauge->count() == 45;
    for (auto index = 1; index < structuredGauge->count(); ++index)
        structuredGaugeRangeValid = structuredGaugeRangeValid
            && structuredGauge->itemData(index).toInt() == index - 4;
    structuredGauge->setCurrentIndex(structuredGauge->findData(20));
    wireEditor.show(); application.processEvents();
    const auto structuredGaugeCommitted = std::abs(
        structuredGaugeRadius - necwb::model::awgRadiusMeters(20)) < 1.0e-12;
    wireTable->item(0, 8)->setText(QStringLiteral("0.002"));
    application.processEvents();
    auto* customGauge = qobject_cast<QComboBox*>(wireTable->cellWidget(0, 9));
    const auto customRadiusPreserved = customGauge != nullptr && customGauge->currentIndex() == 0;
    necwb::ui::WireCardEditor symbolicWireEditor;
    symbolicWireEditor.setSymbolicGeometryFields({{1, {{3, "height"}, {4, "2*3"}}}});
    symbolicWireEditor.setSymbols({{"height", "10", 10.0, 1}, {"len", "4", 4.0, 1},
        {"dlen", "0.1", 0.1, 1}, {"rlen", "4.123456789012", 4.123456789012, 1}});
    symbolicWireEditor.setModel(wireModel);
    std::vector<std::pair<std::size_t, QString>> expressionsEntered;
    auto symbolicWireEdited = false;
    QObject::connect(symbolicWireEditor.symbolCells(), &necwb::ui::SymbolCells::fieldExpressionEntered,
        [&](std::size_t, std::size_t field, const QString&, const QString& expression, necwb::ui::PromptPlace) {
            expressionsEntered.emplace_back(field, expression);
        });
    QObject::connect(&symbolicWireEditor, &necwb::ui::WireCardEditor::wireEdited,
        [&](const auto&, const auto&) { symbolicWireEdited = true; });
    auto* symbolicWireTable = symbolicWireEditor.findChild<QTableWidget*>(
        QStringLiteral("wireCardTable"));
    auto* symbolicGauge = symbolicWireTable == nullptr ? nullptr
        : qobject_cast<QComboBox*>(symbolicWireTable->cellWidget(0, 9));
    const auto symbolicGaugeEnabled = symbolicGauge != nullptr && symbolicGauge->isEnabled();
    // An expression cell shows its value, styled, and edits as written with the symbols offered.
    auto* heightCell = symbolicWireTable == nullptr ? nullptr : symbolicWireTable->item(0, 3);
    auto symbolicCoordinatesLocked = heightCell != nullptr
        && (heightCell->flags() & Qt::ItemIsEditable)
        && heightCell->data(necwb::ui::CellExpressionRole).toString() == QStringLiteral("height")
        && !heightCell->icon().isNull() && heightCell->font().italic()
        && heightCell->toolTip().startsWith(QStringLiteral("ƒx height = "));
    if (heightCell != nullptr) {
        symbolicWireEditor.show();
        symbolicWireTable->editItem(heightCell);
        auto* cellEditor = symbolicWireTable->viewport()->findChild<QLineEdit*>();
        auto* completer = cellEditor == nullptr ? nullptr : cellEditor->findChild<QCompleter*>();
        symbolicCoordinatesLocked = symbolicCoordinatesLocked && cellEditor != nullptr
            && cellEditor->text() == QStringLiteral("height") && completer != nullptr
            && completer->model()->index(0, 0).data().toString() == QStringLiteral("height = 10");
        // The symbols match anywhere in the name, and only the word being typed is completed.
        if (completer != nullptr) {
            const auto offered = [completer] {
                QStringList names;
                for (auto row = 0; completer->setCurrentRow(row); ++row) names << completer->currentCompletion();
                return names;
            };
            cellEditor->clear();
            QTest::keyClicks(cellEditor, QStringLiteral("LEN"));
            const auto midName = offered() == QStringList{QStringLiteral("len"), QStringLiteral("dlen"),
                QStringLiteral("rlen")};
            // The list is as wide as its longest "name = value", past the narrower cell.
            const auto* popup = completer->popup();
            const auto longest = popup->fontMetrics().horizontalAdvance(QStringLiteral("rlen = 4.123456789012"));
            const auto popupWide = popup->isVisible() && popup->width() >= longest
                && longest > cellEditor->width();
            if (!popupWide) qWarning() << "completer width" << popup->isVisible() << popup->width() << longest << cellEditor->width();
            cellEditor->setText(QStringLiteral("len/"));
            QTest::keyClicks(cellEditor, QStringLiteral("h"));
            const auto wordOnly = completer->completionPrefix() == QStringLiteral("h")
                && offered() == QStringList{QStringLiteral("height")};
            emit completer->activated(QStringLiteral("height"));
            const auto wordReplaced = cellEditor->text() == QStringLiteral("len/height");
            if (!midName || !wordOnly || !wordReplaced)
                qWarning() << "symbol completer" << midName << wordOnly << wordReplaced << offered()
                           << cellEditor->text();
            symbolicCoordinatesLocked = symbolicCoordinatesLocked && midName && popupWide && wordOnly && wordReplaced;
            cellEditor->setText(QStringLiteral("height"));
        }
        symbolicWireTable->closePersistentEditor(heightCell);
        // A name, a number over an expression, the expression unchanged, and a plain number.
        symbolicWireTable->item(0, 2)->setText(QStringLiteral("len/2"));
        application.processEvents();
        symbolicWireTable->item(0, 4)->setText(QStringLiteral("5"));
        application.processEvents();
        symbolicWireTable->item(0, 3)->setText(QStringLiteral("height"));
        application.processEvents();
        symbolicWireTable->item(0, 5)->setText(QStringLiteral("4"));
        application.processEvents();
        symbolicCoordinatesLocked = symbolicCoordinatesLocked && symbolicWireEdited
            && expressionsEntered == std::vector<std::pair<std::size_t, QString>>{
                {2, QStringLiteral("len/2")}, {4, QStringLiteral("5")}};
        symbolicWireEditor.hide();
    }
    QImage wireImage(900, 400, QImage::Format_ARGB32_Premultiplied);
    wireImage.fill(Qt::transparent); QPainter wirePainter(&wireImage);
    wireEditor.render(&wirePainter); wirePainter.end();
    necwb::model::AntennaModel gaugeModel;
    const auto gaugeRadius = necwb::model::awgRadiusMeters(12);
    const necwb::model::Wire gaugeWire{2, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
        11, gaugeRadius, 1};
    gaugeModel.addWire(gaugeWire);
    // An AWG radius shows as its gauge, and the line under the table gives its diameter.
    necwb::ui::WireCardEditor gaugeTable;
    gaugeTable.setModel(gaugeModel);
    gaugeTable.selectWire(2);
    auto* gaugeEditor = gaugeTable.findChild<QComboBox*>(QStringLiteral("wireGaugeEditor"));
    const auto* gaugeNote = gaugeTable.findChild<QLabel*>(QStringLiteral("cardRowNote"));
    const auto gaugeDropdownValid = gaugeEditor != nullptr && gaugeEditor->currentData().toInt() == 12
        && gaugeNote != nullptr && gaugeNote->text().startsWith(QString::fromUtf8("Ø 0.00205"))
        && gaugeNote->text().contains(QStringLiteral("AWG"));
    if (!gaugeDropdownValid) qWarning() << "gauge" << (gaugeNote ? gaugeNote->text() : QString{});
    QTabWidget setupHost;
    necwb::ui::SetupEditor setupEditor(&setupHost);
    setupHost.addTab(setupEditor.referenceImpedancePage(), QStringLiteral("Z0"));
    setupHost.addTab(setupEditor.environmentPage(), QStringLiteral("Environment"));
    setupHost.addTab(setupEditor.frequencyPage(), QStringLiteral("Frequency"));
    setupHost.resize(900, 650);
    setupHost.show();
    application.processEvents();
    const auto setupColumnsValid = setupHost.count() == 3
        && !setupEditor.isVisible()
        && setupEditor.referenceImpedancePage()->objectName() == QStringLiteral("referenceImpedancePage")
        && setupEditor.environmentPage()->objectName() == QStringLiteral("environmentEditorPage")
        && setupEditor.frequencyPage()->objectName() == QStringLiteral("frequencyEditorPage");
    auto* groundPreset = setupEditor.environmentPage()->findChild<QComboBox*>(
        QStringLiteral("groundPresetControl"));
    auto* groundPermittivity = setupEditor.environmentPage()->findChild<QDoubleSpinBox*>(
        QStringLiteral("groundRelativePermittivity"));
    auto* groundConductivity = setupEditor.environmentPage()->findChild<QDoubleSpinBox*>(
        QStringLiteral("groundConductivity"));
    auto* sourceReferenceImpedance = setupEditor.referenceImpedancePage()->findChild<QDoubleSpinBox*>(
        QStringLiteral("sourceReferenceImpedance"));
    auto* applyReferenceImpedance = setupEditor.referenceImpedancePage()->findChild<QPushButton*>(
        QStringLiteral("applyReferenceImpedanceButton"));
    if (groundPreset == nullptr || groundPermittivity == nullptr
        || groundConductivity == nullptr || sourceReferenceImpedance == nullptr
        || applyReferenceImpedance == nullptr)
        return NECWB_SMOKE_FAILURE("model setup controls");
    auto referenceImpedanceSignalValid = false;
    QObject::connect(&setupEditor, &necwb::ui::SetupEditor::referenceImpedanceChanged,
        [&referenceImpedanceSignalValid](const auto& reference) {
            referenceImpedanceSignalValid = reference.sourceLine == 0
                && std::abs(reference.ohms - 200.0) < 1.0e-12;
        });
    sourceReferenceImpedance->setValue(200.0);
    const auto referenceImpedancePending = setupEditor.hasPendingEdits(
        setupEditor.referenceImpedancePage())
        && applyReferenceImpedance->property("pendingChanges").toBool();
    applyReferenceImpedance->click();
    const auto referenceImpedanceControlValid = referenceImpedanceSignalValid
        && !setupEditor.hasPendingEdits(setupEditor.referenceImpedancePage());
    const auto saltWaterIndex = groundPreset->findText(
        QStringLiteral("Salt water"), Qt::MatchStartsWith);
    groundPreset->setCurrentIndex(saltWaterIndex);
    const auto groundPresetValuesValid = groundPreset->count() == 10
        && saltWaterIndex > 0
        && std::abs(groundPermittivity->value() - 81.0) < 1.0e-9
        && std::abs(groundConductivity->value() - 5.0) < 1.0e-12;
    groundConductivity->setValue(4.5);
    const auto customGroundDetected = groundPreset->currentIndex() == 0;
    auto* applyGround = setupEditor.environmentPage()->findChild<QPushButton*>(
        QStringLiteral("applyGroundButton"));
    const auto groundApplyHighlighted = applyGround != nullptr
        && applyGround->property("pendingChanges").toBool()
        && setupEditor.hasPendingEdits(setupEditor.environmentPage());
    setupEditor.discardPendingEdits(setupEditor.environmentPage());
    const auto groundDiscardRestored = !setupEditor.hasPendingEdits(
        setupEditor.environmentPage())
        && std::abs(groundConductivity->value() - 0.005) < 1.0e-12;
    // The setup pages fit the column at its narrowest without ⇔; a wider page shows it.
    auto setupPagesFit = true;
    NECWB_SMOKE_STEP();
    {
        necwb::ui::PropertiesPane fitPane;
        fitPane.resize(360, 600);
        necwb::ui::AnalysisSetupEditor solverPage;
        QWidget widePage;
        widePage.setMinimumWidth(800);
        fitPane.show();
        auto* fitExpand = fitPane.findChild<QToolButton*>(QStringLiteral("paneExpandButton"));
        for (auto* page : std::initializer_list<QWidget*>{setupEditor.frequencyPage(),
                 setupEditor.environmentPage(), setupEditor.referenceImpedancePage(), &solverPage}) {
            page->setParent(nullptr);
            fitPane.showPage(page, QStringLiteral("Setup"));
            application.processEvents();
            if (fitPane.fullWidth() > 360 || fitExpand->isVisible()) {
                qWarning() << "setup page too wide" << page->objectName() << fitPane.fullWidth();
                setupPagesFit = false;
            }
        }
        fitPane.showPage(&widePage, QStringLiteral("Wide"));
        application.processEvents();
        setupPagesFit = setupPagesFit && fitExpand->isVisible() && fitPane.fullWidth() > 800;
        for (auto* page : {setupEditor.frequencyPage(), setupEditor.environmentPage(),
                 setupEditor.referenceImpedancePage()}) page->setParent(&setupEditor);
        widePage.setParent(nullptr);
        solverPage.setParent(nullptr);
    }
    necwb::ui::ParameterEditor parameterEditor;
    const auto parameterEditorCompact = hasCompactMargins(parameterEditor, 8);
    parameterEditor.setResolution(necwb::nec::NecSymbolResolver{}.resolve(
        "SY LENGTH=10, HALF=LENGTH/2\nGW 1 11 -HALF 0 0 HALF 0 0 .001\nGE 0\nEN\n"));
    parameterEditor.resize(760, 520);
    parameterEditor.show();
    application.processEvents();
    auto* parameterTable = parameterEditor.findChild<QTableWidget*>(QStringLiteral("parameterTable"));
    auto* applyParameter = parameterEditor.findChild<QPushButton*>(
        QStringLiteral("applyParameterButton"));
    auto* revertParameter = parameterEditor.findChild<QPushButton*>(
        QStringLiteral("revertParameterButton"));
    auto parameterSignalValid = false;
    QObject::connect(&parameterEditor, &necwb::ui::ParameterEditor::parameterChanged,
        [&parameterSignalValid](std::size_t sourceLine, const QString& originalName,
            const QString& name, const QString& expression) {
            parameterSignalValid = sourceLine == 1 && originalName == QStringLiteral("HALF")
                && name == QStringLiteral("HALF") && expression == QStringLiteral("LENGTH/2+1");
        });
    if (parameterTable == nullptr || applyParameter == nullptr
        || revertParameter == nullptr)
        return NECWB_SMOKE_FAILURE("parameter editor controls");
    parameterTable->selectRow(1);
    application.processEvents();
    parameterTable->item(1, 1)->setText(QStringLiteral("LENGTH/2+1"));
    parameterTable->selectRow(0);
    application.processEvents();
    const auto parameterEditPending = parameterEditor.hasPendingEdits()
        && parameterTable->currentRow() == 1
        && applyParameter->isEnabled() && revertParameter->isEnabled()
        && applyParameter->property("pendingChanges").toBool()
        && parameterTable->item(1, 2)->text() == QStringLiteral("Apply to resolve");
    applyParameter->click();
    revertParameter->click();
    const auto parameterRevertValid = !parameterEditor.hasPendingEdits()
        && parameterTable->item(1, 1)->text() == QStringLiteral("LENGTH/2")
        && parameterTable->item(1, 2)->text() == QStringLiteral("5.000");
    const auto parameterEditorValid = parameterTable->columnCount() == 3
        && parameterTable->rowCount() == 2
        && parameterTable->item(0, 0)->text() == QStringLiteral("LENGTH")
        && parameterTable->item(1, 2)->text() == QStringLiteral("5.000")
        && parameterTable->editTriggers().testFlag(QAbstractItemView::DoubleClicked)
        && !parameterTable->item(1, 2)->flags().testFlag(Qt::ItemIsEditable)
        && parameterEditPending && parameterRevertValid && parameterSignalValid;
    necwb::ui::ParameterEditor emptyParameterEditor;
    emptyParameterEditor.setResolution(necwb::nec::NecSymbolResolver{}.resolve(
        "GW 1 11 -1 0 0 1 0 0 .001\nGE 0\nEN\n"));
    auto* emptyParameterTable = emptyParameterEditor.findChild<QTableWidget*>(
        QStringLiteral("parameterTable"));
    auto* addParameter = emptyParameterEditor.findChild<QPushButton*>(
        QStringLiteral("addParameterButton"));
    auto* applyNewParameter = emptyParameterEditor.findChild<QPushButton*>(
        QStringLiteral("applyParameterButton"));
    auto newParameterSignalValid = false;
    QObject::connect(&emptyParameterEditor, &necwb::ui::ParameterEditor::parameterChanged,
        [&newParameterSignalValid](std::size_t sourceLine, const QString& originalName,
            const QString& name, const QString& expression) {
            newParameterSignalValid = sourceLine == 0 && originalName.isEmpty()
                && name == QStringLiteral("LENGTH") && expression == QStringLiteral("10");
        });
    if (emptyParameterTable == nullptr || addParameter == nullptr
        || applyNewParameter == nullptr || !addParameter->isEnabled())
        return NECWB_SMOKE_FAILURE("new parameter controls");
    addParameter->click();
    emptyParameterTable->item(0, 0)->setText(QStringLiteral("LENGTH"));
    emptyParameterTable->item(0, 1)->setText(QStringLiteral("10"));
    const auto emptyParameterDraftValid = emptyParameterTable->rowCount() == 1
        && applyNewParameter->isEnabled() && emptyParameterEditor.hasPendingEdits();
    applyNewParameter->click();
    const auto newParameterValid = emptyParameterDraftValid && newParameterSignalValid;
    necwb::model::ModelSetup requestSetup;
    requestSetup.frequency = necwb::model::FrequencyDefinition{0, 1, 14.15, 0.0, 4};
    requestSetup.radiationPatterns = {
        {37, 36, 0.0, 0.0, 5.0, 10.0, 5},
        {1, 360, 62.0, 0.0, 0.0, 1.0, 6}};
    necwb::ui::AnalysisRequestEditor requestEditor;
    const auto requestEditorCompact = hasCompactMargins(requestEditor, 12);
    requestEditor.setData(requestSetup);
    requestEditor.resize(900, 760);
    requestEditor.show();
    application.processEvents();
    auto* requestTable = requestEditor.findChild<QTableWidget*>(
        QStringLiteral("radiationRequestsTable"));
    auto* addPattern = requestEditor.findChild<QPushButton*>(
        QStringLiteral("addRadiationPatternButton"));
    auto* applyPattern = requestEditor.findChild<QPushButton*>(
        QStringLiteral("applyRadiationPatternButton"));
    auto* deletePattern = requestEditor.findChild<QPushButton*>(
        QStringLiteral("deleteRadiationPatternButton"));
    auto* patternType = requestEditor.findChild<QComboBox*>(
        QStringLiteral("radiationPatternType"));
    auto* resetPattern = requestEditor.findChild<QPushButton*>(
        QStringLiteral("resetRadiationPatternButton"));
    auto* thetaStart = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationThetaStart"));
    auto* thetaEnd = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationThetaEnd"));
    auto* phiEnd = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationPhiEnd"));
    auto* frequencyPolicyGroup = requestEditor.findChild<QGroupBox*>(
        QStringLiteral("radiationFrequencyPolicyGroup"));
    auto* patternFrequencyMode = requestEditor.findChild<QComboBox*>(
        QStringLiteral("radiationFrequencyMode"));
    auto* patternSingleFrequency = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationSingleFrequency"));
    auto* patternContinuousStart = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationContinuousStart"));
    auto* patternContinuousStop = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationContinuousStop"));
    auto* patternContinuousStep = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationContinuousStep"));
    auto* patternSelectedFrequencies = requestEditor.findChild<QListWidget*>(
        QStringLiteral("radiationSelectedFrequencies"));
    auto* addPatternBandCenters = requestEditor.findChild<QPushButton*>(
        QStringLiteral("radiationAddAmateurBandCenters"));
    auto* applyPatternFrequencies = requestEditor.findChild<QPushButton*>(
        QStringLiteral("applyRadiationFrequenciesButton"));
    auto* resetPatternFrequencies = requestEditor.findChild<QPushButton*>(
        QStringLiteral("resetRadiationFrequenciesButton"));
    auto* clearPatternFrequencies = requestEditor.findChild<QPushButton*>(
        QStringLiteral("radiationClearFrequencies"));
    auto* selectedPatternGroup = requestEditor.findChild<QGroupBox*>(
        QStringLiteral("selectedRadiationPatternGroup"));
    auto* thetaGroup = requestEditor.findChild<QGroupBox*>(QStringLiteral("radiationThetaGroup"));
    auto* phiGroup = requestEditor.findChild<QGroupBox*>(QStringLiteral("radiationPhiGroup"));
    if (requestTable == nullptr || addPattern == nullptr || applyPattern == nullptr
        || deletePattern == nullptr
        || patternType == nullptr || resetPattern == nullptr || thetaStart == nullptr
        || thetaEnd == nullptr || phiEnd == nullptr || frequencyPolicyGroup == nullptr
        || patternFrequencyMode == nullptr || patternSingleFrequency == nullptr
        || patternContinuousStart == nullptr || patternContinuousStop == nullptr
        || patternContinuousStep == nullptr || patternSelectedFrequencies == nullptr
        || addPatternBandCenters == nullptr || applyPatternFrequencies == nullptr
        || resetPatternFrequencies == nullptr
        || clearPatternFrequencies == nullptr
        || selectedPatternGroup == nullptr || thetaGroup == nullptr || phiGroup == nullptr)
        return NECWB_SMOKE_FAILURE("analysis request controls");
    const auto patternRequestLayoutValid = frequencyPolicyGroup->geometry().top()
            < requestTable->geometry().top()
        && thetaGroup->geometry().top() == phiGroup->geometry().top()
        && thetaGroup->geometry().left() < phiGroup->geometry().left();
    const auto patternFrequenciesInitializeFromModel = patternFrequencyMode->currentData().toInt() == 0
        && std::abs(patternSingleFrequency->value() - 14.15) < 1.0e-9
        && patternSelectedFrequencies->count() == 1
        && std::abs(patternSelectedFrequencies->item(0)->data(Qt::UserRole).toDouble()
            - 14.15) < 1.0e-9;
    const auto frequencyApplyInitiallyClean =
        !applyPatternFrequencies->property("pendingChanges").toBool();
    auto emittedPattern = necwb::model::RadiationPatternRequest{};
    QObject::connect(&requestEditor, &necwb::ui::AnalysisRequestEditor::patternChanged,
        [&emittedPattern](const auto& pattern) { emittedPattern = pattern; });
    requestTable->selectRow(1);
    application.processEvents();
    thetaStart->setValue(63.0);
    thetaEnd->setValue(63.0);
    const auto applyPatternHighlightsPending =
        applyPattern->property("pendingChanges").toBool();
    const auto customizedHorizontalRemainsHorizontal = patternType->currentData().toInt() == 1
        && resetPattern->isEnabled();
    resetPattern->click();
    const auto horizontalPresetRestored = thetaStart->value() == 90.0
        && thetaEnd->value() == 90.0 && patternType->currentData().toInt() == 1;
    requestTable->selectRow(0);
    application.processEvents();
    phiEnd->setValue(0.0);
    const auto unclassifiedGridBecomesCustom = patternType->currentData().toInt() == 3
        && resetPattern->isEnabled();
    resetPattern->click();
    const auto fullGridPresetRestored = patternType->currentData().toInt() == 0
        && phiEnd->value() == 355.0;
    addPattern->click();
    patternType->setCurrentIndex(patternType->findData(1));
    const auto patternDraftPreviewVisible = requestTable->item(
            requestTable->currentRow(), 0)->text().contains(QStringLiteral("Azimuth cut"))
        && requestTable->item(requestTable->currentRow(), 0)->text().contains(
            QStringLiteral("Draft"));
    applyPattern->click();
    const auto applyPatternClearsPending =
        !applyPattern->property("pendingChanges").toBool()
        && !requestTable->item(requestTable->currentRow(), 0)->text().contains(
            QStringLiteral("Draft"));
    const auto patternEditorBaseValid = requestTable->rowCount() == 3
        && requestTable->item(0, 0)->text() == QStringLiteral("Full 3D pattern")
        && requestTable->item(1, 0)->text() == QStringLiteral("Azimuth cut")
        && emittedPattern.sourceLine == 0 && emittedPattern.thetaCount == 1
        && emittedPattern.phiCount == 360 && emittedPattern.phiStep == 1.0;
    auto* cutAngle = requestEditor.findChild<QDoubleSpinBox*>(
        QStringLiteral("radiationCutAngle"));
    auto* cutAngleHint = requestEditor.findChild<QLabel*>(
        QStringLiteral("radiationCutAngleHint"));
    auto* cutsThroughPeak = requestEditor.findChild<QPushButton*>(
        QStringLiteral("addCutsThroughPeakButton"));
    auto cutAngleEntersElevation = false;
    auto cutsThroughPeakAdded = false;
    if (cutAngle != nullptr && cutAngleHint != nullptr && cutsThroughPeak != nullptr) {
        requestTable->selectRow(1);
        application.processEvents();
        const auto showsElevation = cutAngle->isVisibleTo(&requestEditor)
            && cutAngle->value() == 28.0 && cutAngleHint->text() == QStringLiteral("NEC θ 62°");
        cutAngle->setValue(10.0);
        cutAngleEntersElevation = showsElevation && thetaStart->value() == 80.0
            && thetaEnd->value() == 80.0 && cutAngleHint->text() == QStringLiteral("NEC θ 80°");
        requestEditor.discardPendingEdits();
        const auto disabledWithoutRun = !cutsThroughPeak->isEnabled();
        std::vector<necwb::model::RadiationPatternRequest> addedCuts;
        const auto connection = QObject::connect(&requestEditor,
            &necwb::ui::AnalysisRequestEditor::patternChanged,
            [&addedCuts](const auto& pattern) { addedCuts.push_back(pattern); });
        requestEditor.setLatestPatternPeak(necwb::analysis::PatternDirection{12.0, 30.0});
        cutsThroughPeak->click();
        QObject::disconnect(connection);
        cutsThroughPeakAdded = disabledWithoutRun && addedCuts.size() == 2
            && addedCuts[0].thetaCount == 1 && addedCuts[0].thetaStart == 78.0
            && addedCuts[0].phiCount == 360 && addedCuts[0].phiStep == 1.0
            && addedCuts[1].thetaCount == 181 && addedCuts[1].phiCount == 2
            && addedCuts[1].phiStart == 30.0 && addedCuts[1].phiStep == 180.0;
    }
    patternFrequencyMode->setCurrentIndex(patternFrequencyMode->findData(3));
    patternContinuousStart->setValue(7.0);
    patternContinuousStop->setValue(7.2);
    patternContinuousStep->setValue(0.1);
    const auto frequencyApplyHighlightsPending =
        applyPatternFrequencies->property("pendingChanges").toBool()
        && requestEditor.hasPendingEdits();
    applyPatternFrequencies->click();
    const auto continuousPatternPlan = requestEditor.radiationFrequencyPlan();
    const auto continuousPatternPoints =
        necwb::analysis::frequencyPlanPoints(continuousPatternPlan);
    patternFrequencyMode->setCurrentIndex(patternFrequencyMode->findData(1));
    patternSingleFrequency->setValue(14.2);
    applyPatternFrequencies->click();
    const auto singlePatternPoints = necwb::analysis::frequencyPlanPoints(
        requestEditor.radiationFrequencyPlan());
    patternFrequencyMode->setCurrentIndex(patternFrequencyMode->findData(2));
    patternSelectedFrequencies->clear();
    auto bandCenterDialogValid = false;
    QTimer::singleShot(0, &requestEditor, [&requestEditor, &bandCenterDialogValid] {
        auto* dialog = requestEditor.findChild<QDialog*>(
            QStringLiteral("radiationAmateurBandCenterDialog"));
        if (dialog == nullptr) return;
        auto* twentyMeters = dialog->findChild<QCheckBox*>(
            QStringLiteral("radiationAmateurBandCenterCheck4"));
        auto* addSelected = dialog->findChild<QPushButton*>(
            QStringLiteral("radiationAddSelectedBandCenters"));
        auto* summary = dialog->findChild<QLabel*>(
            QStringLiteral("radiationAmateurBandCenterSummary"));
        if (twentyMeters == nullptr || addSelected == nullptr || summary == nullptr) return;
        const auto noneTicked = !addSelected->isEnabled()
            && summary->text() == QStringLiteral("0 center frequency point(s)");
        twentyMeters->setChecked(true);
        bandCenterDialogValid = noneTicked && addSelected->isEnabled()
            && twentyMeters->text().contains(QStringLiteral("(14.175 MHz)"))
            && summary->text() == QStringLiteral("1 center frequency point(s)");
        addSelected->click();
    });
    addPatternBandCenters->click();
    application.processEvents();
    const auto bandCentersPopulateList = bandCenterDialogValid
        && patternSelectedFrequencies->count() == 1
        && patternSelectedFrequencies->item(0)->text().contains(QStringLiteral("14.175"))
        && patternSelectedFrequencies->viewMode() == QListView::IconMode
        && patternSelectedFrequencies->visualItemRect(
            patternSelectedFrequencies->item(0)).isValid()
        && applyPatternFrequencies->property("pendingChanges").toBool();
    applyPatternFrequencies->click();
    const auto bandCenterPoints = necwb::analysis::frequencyPlanPoints(
        requestEditor.radiationFrequencyPlan());
    clearPatternFrequencies->click();
    const auto clearAllEmptiesList = patternSelectedFrequencies->count() == 0
        && requestEditor.hasPendingEdits() && !applyPatternFrequencies->isEnabled();
    requestEditor.discardPendingEdits();
    const auto discardedFrequencyEditRestored = patternSelectedFrequencies->count() == 1
        && !requestEditor.hasPendingEdits();
    auto deletedPatternLine = std::size_t{};
    QObject::connect(&requestEditor,
        &necwb::ui::AnalysisRequestEditor::patternDeleteRequested,
        [&deletedPatternLine](std::size_t sourceLine) { deletedPatternLine = sourceLine; });
    requestTable->selectRow(0);
    deletePattern->click();
    const auto deletionDraftVisible = requestTable->item(0, 0)->text().contains(
            QStringLiteral("Pending deletion"))
        && deletePattern->text() == QStringLiteral("Restore Pattern")
        && applyPattern->property("pendingChanges").toBool()
        && deletedPatternLine == 0;
    deletePattern->click();
    const auto deletionDraftRestores = !requestTable->item(0, 0)->text().contains(
            QStringLiteral("Pending deletion"))
        && deletePattern->text() == QStringLiteral("Delete Pattern")
        && !requestEditor.hasPendingEdits();
    deletePattern->click();
    applyPattern->click();
    const auto deletionAppliesExplicitly = deletedPatternLine == 5;
    requestEditor.setData(requestSetup);
    const auto sameModelRefreshPreservesPatternFrequencies =
        patternFrequencyMode->currentData().toInt() == 2
        && patternSelectedFrequencies->count() == 1
        && std::abs(patternSelectedFrequencies->item(0)->data(Qt::UserRole).toDouble()
            - 14.175) < 1.0e-9;
    resetPatternFrequencies->click();
    const auto resetActionRestoresModelFrequencies = patternFrequencyMode->currentData().toInt() == 0
        && std::abs(patternSingleFrequency->value() - 14.15) < 1.0e-9
        && !requestEditor.hasPendingEdits();
    auto newRequestSetup = requestSetup;
    newRequestSetup.frequency = necwb::model::FrequencyDefinition{0, 1, 300.0, 0.0, 4};
    requestEditor.resetFrequencySelectionForModel();
    requestEditor.setData(newRequestSetup);
    const auto newModelResetsPatternFrequencies = patternFrequencyMode->currentData().toInt() == 0
        && std::abs(patternSingleFrequency->value() - 300.0) < 1.0e-9
        && patternSelectedFrequencies->count() == 1
        && std::abs(patternSelectedFrequencies->item(0)->data(Qt::UserRole).toDouble()
            - 300.0) < 1.0e-9;
    const auto patternFrequencySelectionValid = continuousPatternPoints.size() == 3
        && std::abs(continuousPatternPoints[1] - 7.1) < 1.0e-9
        && singlePatternPoints.size() == 1
        && std::abs(singlePatternPoints.front() - 14.2) < 1.0e-9
        && bandCenterPoints.size() == 1
        && std::abs(bandCenterPoints.front() - 14.175) < 1.0e-9;
    const auto multiplePatternEditorValid = patternEditorBaseValid && patternRequestLayoutValid
        && customizedHorizontalRemainsHorizontal && horizontalPresetRestored
        && unclassifiedGridBecomesCustom && fullGridPresetRestored
        && frequencyApplyInitiallyClean && applyPatternHighlightsPending
        && patternDraftPreviewVisible && applyPatternClearsPending
        && frequencyApplyHighlightsPending && bandCentersPopulateList
        && clearAllEmptiesList && discardedFrequencyEditRestored
        && deletionDraftVisible && deletionDraftRestores && deletionAppliesExplicitly
        && patternFrequencySelectionValid && patternFrequenciesInitializeFromModel
        && sameModelRefreshPreservesPatternFrequencies && resetActionRestoresModelFrequencies
        && newModelResetsPatternFrequencies && cutAngleEntersElevation && cutsThroughPeakAdded;
    auto* shutdownRequestEditor = new necwb::ui::AnalysisRequestEditor;
    shutdownRequestEditor->setData(requestSetup);
    auto* shutdownFrequencyMode = shutdownRequestEditor->findChild<QComboBox*>(
        QStringLiteral("radiationFrequencyMode"));
    auto* shutdownFrequencyList = shutdownRequestEditor->findChild<QListWidget*>(
        QStringLiteral("radiationSelectedFrequencies"));
    if (shutdownFrequencyMode == nullptr || shutdownFrequencyList == nullptr)
        return NECWB_SMOKE_FAILURE("analysis request shutdown");
    shutdownFrequencyMode->setCurrentIndex(shutdownFrequencyMode->findData(2));
    shutdownFrequencyList->clear();
    delete shutdownRequestEditor;
    application.processEvents();
    necwb::model::AntennaModel attachmentModel;
    attachmentModel.addWire({1, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 11, 0.001, 1});
    attachmentModel.addWire({2, {-1.0, 1.0, 0.0}, {1.0, 1.0, 0.0}, 11, 0.001, 2});
    necwb::ui::AutoSegmentationDialog segmentationDialog(
        "GW 1 11 -1 0 0 1 0 0 0.001\nGW 2 11 -1 1 0 1 1 0 0.001\nGE 0\nEN\n",
        attachmentModel, {}, 30.0);
    auto* applySegmentation = segmentationDialog.findChild<QPushButton*>(
        QStringLiteral("applySegmentationButton"));
    if (applySegmentation == nullptr)
        return NECWB_SMOKE_FAILURE("automatic segmentation controls");
    applySegmentation->click();
    application.processEvents();
    const auto segmentationApplyAccepted = segmentationDialog.result() == QDialog::Accepted;
    // In small side views of a yagi, the wire and EX labels are drawn whole inside the view,
    // clear of the EX marker and of each other, though the elements end on end draw them at
    // one point; a label at a wire end by the edge goes on the inner side.
    NECWB_SMOKE_STEP();
    {
        necwb::model::AntennaModel yagi;
        yagi.addWire({1, {-0.5, -0.52, 0.0}, {-0.5, 0.52, 0.0}, 11, 0.003, 1});
        yagi.addWire({2, {0.0, -0.5, 0.0}, {0.0, 0.5, 0.0}, 11, 0.003, 2});
        yagi.addWire({3, {0.55, -0.46, 0.0}, {0.55, 0.46, 0.0}, 11, 0.003, 3});
        for (const auto plane : {necwb::geometry::ProjectionPlane::XZ, necwb::geometry::ProjectionPlane::YZ}) {
            necwb::ui::GeometryView side(plane);
            side.resize(side.minimumSizeHint().width(), 140);
            side.setModel(yagi);
            side.setExcitations({{0, 2, 6, 1.0, 0.0, 5}});
            side.fitToView();
            // Zoomed in about the middle, the outer wire ends sit by the edges.
            QWheelEvent zoomIn(QPointF(side.width() / 2.0, side.height() / 2.0),
                side.mapToGlobal(QPointF(side.width() / 2.0, side.height() / 2.0)), {}, {0, 300},
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&side, &zoomIn);
            QImage sideImage(side.size(), QImage::Format_ARGB32_Premultiplied);
            side.render(&sideImage);
            const auto& boxes = side.labelBoxes();
            // The EX diamond with its outline, and every wire end's dot.
            const auto source = side.screenPoint({0.0, 0.0, 0.0});
            std::vector<QRectF> marks{QRectF(source - QPointF(9.0, 9.0), QSizeF(18.0, 18.0))};
            for (const auto& wire : yagi.wires())
                for (const auto& end : {wire.start, wire.end})
                    marks.emplace_back(side.screenPoint(end) - QPointF(3.5, 3.5), QSizeF(7.0, 7.0));
            const auto edge = plane == necwb::geometry::ProjectionPlane::XZ
                ? side.screenPoint({0.55, 0.0, 0.0}) : side.screenPoint({0.0, 0.52, 0.0});
            auto labelsClear = boxes.size() == 4 && edge.x() + 40.0 > side.width();
            for (auto first = std::size_t{}; first < boxes.size(); ++first) {
                labelsClear = labelsClear && QRectF(side.rect()).contains(boxes[first])
                    && std::ranges::none_of(marks, [&](const QRectF& mark) { return mark.intersects(boxes[first]); });
                for (auto second = first + 1; second < boxes.size(); ++second)
                    labelsClear = labelsClear && !boxes[first].intersects(boxes[second]);
            }
            if (!labelsClear) {
                qWarning() << "side view labels" << static_cast<int>(plane) << side.rect() << edge
                           << marks << boxes;
                return NECWB_SMOKE_FAILURE("small view labels inside and apart");
            }
        }
    }
    necwb::model::ModelSetup attachmentSetup;
    attachmentSetup.loads.push_back({4, 1, 6, 6, 50.0, 0.0, 0.0, 7});
    attachmentSetup.transmissionLines.push_back({1, 3, 2, 9, 50.0, 3.048,
        0.0, 0.0, 0.0, 0.0, 8});
    necwb::ui::GeometryView attachment2D(necwb::geometry::ProjectionPlane::XY);
    necwb::ui::Geometry3DView attachment3D;
    attachment2D.resize(700, 450); attachment3D.resize(700, 450);
    attachment2D.setModel(attachmentModel); attachment3D.setModel(attachmentModel);
    attachment2D.setAttachments(attachmentSetup.loads, attachmentSetup.transmissionLines);
    attachment3D.setAttachments(attachmentSetup.loads, attachmentSetup.transmissionLines);
    attachment2D.setPendingTransmissionLineEndpoint(std::pair{1, 2});
    attachment3D.setPendingTransmissionLineEndpoint(std::pair{1, 2});
    attachment2D.selectLoad(7); attachment3D.selectTransmissionLine(8);
    attachment2D.show(); attachment3D.show(); application.processEvents();
    QImage attachmentImage(700, 450, QImage::Format_ARGB32_Premultiplied);
    attachmentImage.fill(Qt::transparent); QPainter attachmentPainter(&attachmentImage);
    attachment2D.render(&attachmentPainter); attachment3D.render(&attachmentPainter);
    attachmentPainter.end();
    NECWB_SMOKE_STEP();
    {
        auto sharedSetup = attachmentSetup;
        sharedSetup.loads.push_back({4, 1, 3, 3, 300.0, 0.0, 0.0, 9});
        sharedSetup.networks.push_back({1, 9, 2, 3, 0.01, 0.0, 0.0, 0.0, 1e10, 0.0, 10});
        necwb::ui::GeometryView network2D(necwb::geometry::ProjectionPlane::XY);
        network2D.resize(400, 300);
        network2D.setModel(attachmentModel);
        network2D.setAttachments(sharedSetup.loads, {}, sharedSetup.networks);
        network2D.setSharedSegments(
            necwb::model::describeSharedSegments(attachmentModel, sharedSetup));
        QImage networkImage(400, 300, QImage::Format_ARGB32_Premultiplied);
        networkImage.fill(Qt::transparent);
        QPainter networkPainter(&networkImage);
        network2D.render(&networkPainter);
        networkPainter.end();
    }
    // A card table: the selected row's type relabels the headers and the line under the table
    // explains what shares its segment; a cell edit changes only its own field, keeping the
    // others exact and an SY symbol as written; a value that is not a number is held back.
    auto cardFormValid = false;
    NECWB_SMOKE_STEP();
    {
        necwb::model::ModelSetup sharedSetup;
        sharedSetup.excitations.push_back({0, 1, 6, 1.0, 0.0, 3});
        sharedSetup.loads.push_back({4, 1, 6, 6, 50.0, 0.0, 0.0, 4});
        const std::string cardSource = "SY r=50\nGW 1 11 0 0 -.5 0 0 .5 .001\nEX 0 1 6 0 1 0\n"
            "LD 4 1 6 6 r 0\nLD 0 1 3 3 12.3456789  0 0\nLD 5 1 1 11 5.8e7\nEN\n";
        const auto cardDocument = necwb::nec::NecParser{}.parse(cardSource);
        const auto cardResolved = necwb::nec::NecParser{}.parse(
            necwb::nec::NecSymbolResolver{}.resolve(cardSource).resolvedSource);
        necwb::ui::StructuredCardEditor loadCards({{QStringLiteral("LD")}});
        loadCards.setDocument(cardDocument, &cardResolved);
        loadCards.setModel(attachmentModel, necwb::model::describeSharedSegments(attachmentModel, sharedSetup));
        loadCards.show();
        application.processEvents();
        QStringList edited;
        QObject::connect(&loadCards, &necwb::ui::StructuredCardEditor::cardEdited,
            [&edited](std::size_t, const QString& text) { edited << text; });
        auto* cells = loadCards.findChild<QTableWidget*>(QStringLiteral("structuredCardTable"));
        const auto* note = loadCards.findChild<QLabel*>(QStringLiteral("cardRowNote"));
        if (cells != nullptr && note != nullptr && cells->rowCount() == 3) {
            const auto headers = [cells] {
                return QStringList{cells->horizontalHeaderItem(6)->text(),
                    cells->horizontalHeaderItem(7)->text(), cells->horizontalHeaderItem(8)->text()};
            };
            const auto usual = headers() == QStringList{QStringLiteral("R Ω"), QStringLiteral("L H"), QStringLiteral("C F")};
            cells->selectRow(0);
            const auto typed = headers() == QStringList{QStringLiteral("R Ω"), QStringLiteral("X Ω"), QStringLiteral("Unused")};
            const auto shared = !note->text().isEmpty();
            cells->selectRow(1);
            const auto series = headers() == QStringList{QStringLiteral("R Ω"), QStringLiteral("L H"), QStringLiteral("C F")}
                && note->text().isEmpty();
            cells->clearSelection();
            const auto cleared = usual && headers() == QStringList{QStringLiteral("R Ω"), QStringLiteral("L H"), QStringLiteral("C F")}
                && note->text().isEmpty();
            cells->item(0, 7)->setText(QStringLiteral("25"));
            cells->item(1, 7)->setText(QStringLiteral("1e-6"));
            application.processEvents();
            // A type 5 load has only σ: its other fields are unused and it edits without them;
            // a field filled in past the card's last one writes zeros up to it.
            cells->selectRow(2);
            const auto conductivity = headers() == QStringList{QString::fromUtf8("σ S/m"),
                QStringLiteral("Unused"), QStringLiteral("Unused")};
            cells->item(2, 6)->setText(QStringLiteral("3.5e7"));
            cells->item(2, 8)->setText(QStringLiteral("1"));
            application.processEvents();
            const auto exact = conductivity && edited == QStringList{QStringLiteral("LD 4 1 6 6 r 25"),
                QStringLiteral("LD 0 1 3 3 12.3456789  1e-6 0"), QStringLiteral("LD 5 1 1 11 3.5e7"),
                QStringLiteral("LD 5 1 1 11 3.5e7 0 1")};
            const auto editedCards = edited;
            edited.clear();
            cells->item(1, 4)->setText(QStringLiteral("x"));
            application.processEvents();
            const auto invalidHeld = edited.isEmpty();
            cardFormValid = typed && shared && series && cleared && exact && invalidHeld;
            if (!cardFormValid)
                qWarning() << "card table" << headers() << typed << shared << series << cleared
                           << exact << editedCards << invalidHeld << note->text();
        }
    }
    QTemporaryDir directory;
    necwb::ui::AnalysisRunStore store(directory.path());
    auto run = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"));
    run.status = QStringLiteral("Completed");
    run.userNote = QStringLiteral("Shortened right side");
    run.durationSeconds = 1.25;
    run.outputBytes = 8192;
    run.frequencyCount = 3;
    run.hasImpedance = true;
    run.hasCurrents = true;
    run.hasRadiation = true;
    run.modelFingerprint = QStringLiteral("baseline-model-fingerprint");
    run.referenceImpedanceOhms = 75.0;
    run.impedanceFrequenciesMHz = {7.0, 14.0, 21.0};
    run.radiationCoverage = {{14.0, 1, 73, "radiation-grid-fingerprint"}};
    if (!store.save(run)) {
        return NECWB_SMOKE_FAILURE("analysis run storage");
    }
    if (store.loadRecent(1).size() != 1 || !store.loadRecent(0).empty())
        return NECWB_SMOKE_FAILURE("recent analysis run storage");
    const QByteArray reviewOutput =
        " FREQUENCY : 1.4100E+01 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 6 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n";
    QFile reviewOutputFile(QDir(run.directory).filePath(QStringLiteral("model.out")));
    QFile reviewSourceFile(QDir(run.directory).filePath(QStringLiteral("model.source.nec")));
    QFile reviewDeckFile(QDir(run.directory).filePath(QStringLiteral("model.nec")));
    const auto reviewArtifactsWritten = reviewOutputFile.open(QIODevice::WriteOnly)
        && reviewOutputFile.write(reviewOutput) == reviewOutput.size()
        && reviewSourceFile.open(QIODevice::WriteOnly)
        && reviewSourceFile.write("CM authored source\nGW 1 11 0 0 0 1 0 0 0.001\nGE 0\nEN\n") > 0
        && reviewDeckFile.open(QIODevice::WriteOnly)
        && reviewDeckFile.write("CM generated deck\nGW 1 11 0 0 0 1 0 0 0.001\nGE 0\nEN\n") > 0;
    reviewOutputFile.close();
    reviewSourceFile.close();
    reviewDeckFile.close();
    necwb::ui::RunReviewWindow runReview;
    const auto runReviewCompact = hasCompactMargins(runReview, 6);
    const auto reviewLoaded = reviewArtifactsWritten
        && runReview.showRun(run, QStringLiteral("active-model.nec"));
    application.processEvents();
    auto* reviewTabs = runReview.findChild<QTabWidget*>(QStringLiteral("runReviewTabs"));
    auto* reviewIdentity = runReview.findChild<QLabel*>(QStringLiteral("runReviewIdentity"));
    auto* reviewActiveModel = runReview.findChild<QLabel*>(QStringLiteral("runReviewActiveModel"));
    auto* reviewSnapshot = runReview.findChild<QWidget*>(
        QStringLiteral("historicalInputSnapshotPanel"));
    auto* reviewUnusedInputPage = runReview.findChild<QWidget*>(
        QStringLiteral("runReviewInputPage"));
    const auto runReviewValid = reviewLoaded && runReview.isVisible()
        && runReview.parentWidget() == nullptr
        && !runReview.testAttribute(Qt::WA_QuitOnClose)
        && reviewTabs != nullptr && reviewTabs->count() == 3
        && reviewTabs->tabText(0) == QStringLiteral("Summary")
        && reviewTabs->tabText(1) == QStringLiteral("Impedance")
        && reviewTabs->tabText(2) == QStringLiteral("Raw Output")
        && reviewIdentity != nullptr
        && reviewIdentity->text().contains(QStringLiteral("test-dipole.nec"))
        && reviewActiveModel != nullptr
        && reviewActiveModel->text().contains(QStringLiteral("active-model.nec"))
        && reviewSnapshot != nullptr && !reviewSnapshot->isHidden()
        && reviewUnusedInputPage != nullptr && reviewUnusedInputPage->isHidden();
    auto* priorReviewPage = reviewTabs == nullptr ? nullptr : reviewTabs->currentWidget();
    necwb::ui::AnalysisRunStore defaultReviewStore;
    auto reviewOptimizationSession = defaultReviewStore.create(QStringLiteral("nec2"),
        QStringLiteral("/tmp/test-dipole.nec"), QStringLiteral("optimization-session"));
    reviewOptimizationSession.status = QStringLiteral("Completed");
    const auto reviewOptimizationSaved = defaultReviewStore.save(reviewOptimizationSession);
    QFile reviewOptimizationMetadata(QDir(reviewOptimizationSession.directory).filePath(
        QStringLiteral("optimization-session.json")));
    const auto reviewOptimizationMetadataWritten = reviewOptimizationMetadata.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        && reviewOptimizationMetadata.write("{}") == 2;
    reviewOptimizationMetadata.close();
    const auto optimizationReviewLoaded = reviewOptimizationSaved
        && reviewOptimizationMetadataWritten
        && runReview.showRun(reviewOptimizationSession,
            QStringLiteral("active-model.nec"));
    application.processEvents();
    auto* reviewOptimizationConfiguration = runReview.findChild<QWidget*>(
        QStringLiteral("optimizationConfigurationPanel"));
    const auto runReviewSwitchesCleanly = optimizationReviewLoaded
        && reviewTabs != nullptr && reviewTabs->count() == 1
        && reviewTabs->tabText(0) == QStringLiteral("Optimization")
        && priorReviewPage != nullptr && priorReviewPage->isHidden()
        && reviewOptimizationConfiguration != nullptr
        && reviewOptimizationConfiguration->isHidden();
    defaultReviewStore.removeGroup(
        reviewOptimizationSession.id, reviewOptimizationSession.directory);
    runReview.close();
    auto session = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"),
        QStringLiteral("optimization-session"));
    session.status = QStringLiteral("Completed");
    session.summary = QStringLiteral("Best candidate: length = 10");
    session.candidateCount = 2;
    if (!store.save(session)) return NECWB_SMOKE_FAILURE("optimization session storage");
    auto candidate = store.create(QStringLiteral("nec2"), QStringLiteral("/tmp/test-dipole.nec"),
        QStringLiteral("optimization-candidate"), session.id);
    candidate.status = QStringLiteral("Completed");
    if (!store.save(candidate)) return NECWB_SMOKE_FAILURE("optimization candidate storage");
    const auto loaded = store.load();
    const auto loadedRun = std::ranges::find(loaded, run.id, &necwb::ui::AnalysisRunRecord::id);
    const auto loadedSession = std::ranges::find(loaded, session.id, &necwb::ui::AnalysisRunRecord::id);
    const auto loadedCandidate = std::ranges::find(loaded, candidate.id, &necwb::ui::AnalysisRunRecord::id);
    const auto storeValid = loaded.size() == 3 && loadedRun != loaded.end()
        && loadedRun->status == QStringLiteral("Completed")
        && loadedRun->sourceFile == QStringLiteral("/tmp/test-dipole.nec")
        && loadedRun->userNote == QStringLiteral("Shortened right side")
        && loadedRun->backend == QStringLiteral("nec2")
        && loadedRun->durationSeconds == 1.25 && loadedRun->outputBytes == 8192
        && loadedRun->frequencyCount == 3 && loadedRun->hasImpedance
        && loadedRun->hasCurrents && loadedRun->hasRadiation
        && loadedRun->modelFingerprint == QStringLiteral("baseline-model-fingerprint")
        && loadedRun->referenceImpedanceOhms == 75.0
        && loadedRun->impedanceFrequenciesMHz == std::vector<double>({7.0, 14.0, 21.0})
        && loadedRun->radiationCoverage.size() == 1
        && loadedRun->radiationCoverage.front().frequencyMHz == 14.0
        && loadedRun->radiationCoverage.front().patternIndex == 1
        && loadedRun->radiationCoverage.front().sampleCount == 73
        && loadedRun->radiationCoverage.front().directionFingerprint
            == "radiation-grid-fingerprint"
        && loadedSession != loaded.end()
        && loadedSession->runType == QStringLiteral("optimization-session")
        && loadedSession->candidateCount == 2
        && loadedCandidate != loaded.end() && loadedCandidate->parentId == session.id;
    const auto runDeletionSafe = !store.remove(directory.path())
        && store.removeGroup(session.id, session.directory)
        && !QFileInfo::exists(session.directory) && !QFileInfo::exists(candidate.directory)
        && store.remove(run.directory) && !QFileInfo::exists(run.directory);
    const auto passed = !image.isNull() && engineeringAxes && candidatePlotValid
        && !summaryImage.isNull()
        && !fieldImage.isNull() && !currentImage.isNull()
        && !modelTreeImage.isNull() && modelTreeStateVisible
        && startPanelValid && storeValid
        && runReviewValid
        && runReviewSwitchesCleanly
        && solverSetupCompact && modelTreeCompact && structuredCardsCompact
        && parameterEditorCompact && requestEditorCompact && runReviewCompact
        && runDeletionSafe
        && !structuredImage.isNull() && invalidEditBlocked && tableNumbers && duplicateFrequencyBlocked
        && structuredSelectionWorks && structuredHierarchyValid
        && descriptiveDropdown
        && trailingFieldsPreserved
        && arcFieldsLabeled && arcEditCommitted && helixFieldsLabeled
        && helixDefaultValid && generatedCardsExcludedFromOther
        && editedCard == QStringLiteral("EX 0 1 7 0 1 0 2.5")
        && addedCard == QStringLiteral("LD 0 1 1 11 0 0 0") && deletedLine == 3
        && !wireImage.isNull() && wireEditCommitted && gaugeDropdownValid
        && structuredGaugeRangeValid && structuredGaugeCommitted && customRadiusPreserved
        && symbolicCoordinatesLocked
        && symbolicGaugeEnabled
        && setupColumnsValid && groundPresetValuesValid && customGroundDetected
        && groundApplyHighlighted && groundDiscardRestored && setupPagesFit
        && referenceImpedancePending && referenceImpedanceControlValid
        && parameterEditorValid && newParameterValid
        && multiplePatternEditorValid
        && convergenceHistoryControlsValid
        && segmentationApplyAccepted
        && peakDirectionSeeded && patternNullExplained && missingDirectionExplained
        && patternPeakRestored
        && radiationControlsSynchronized && radiationSweepSelectable
        && nonPatternFrequencyIgnored && radiationFrequencySynchronized
        && mixedPatternCutsSeparated && horizontalDatasetSelectsCompatibleCut
        && spaceChangesCutInsideView
        && separateDatasetsSwitchCuts
        && horizontalMaxGainCutStaysCompatible && radiationMetricsVisible
        && radiationExportReady && maxGainCutSelected && signedThetaTiesNormalized
        && signedThetaCutStaysOpen && offCutPeakNamed && forwardDirectionReadout
        && crossingCutsPair
        && missingRadiationReported
        && missingRadiationDisablesDataExport
        && resultOriginPreserved && semanticResultExtent
        && !attachmentImage.isNull() && cardFormValid;
    if (!passed) qWarning() << "structured smoke state" << invalidEditBlocked << tableNumbers << editedCard
        << addedCard << deletedLine << "wire committed" << wireEditCommitted
        << "segmentation apply" << segmentationApplyAccepted
        << "run store" << storeValid << "run deletion" << runDeletionSafe
        << "setup pages" << setupColumnsValid << setupPagesFit << referenceImpedancePending
        << referenceImpedanceControlValid
        << "ground presets" << groundPresetValuesValid << customGroundDetected
        << groundPreset->count() << groundPreset->currentIndex()
        << groundPermittivity->value() << groundConductivity->value()
        << "candidate plot" << candidatePlotValid
        << (candidateScorePlot == nullptr ? QVariant{} : candidateScorePlot->property("pointCount"))
        << (candidateScorePlot == nullptr ? QVariant{} : candidateScorePlot->property("bestX"))
        << (candidateScorePlot == nullptr ? QVariant{} : candidateScorePlot->property("leftAxisMinimum"))
        << (candidateScorePlot == nullptr ? QVariant{} : candidateScorePlot->property("leftAxisMaximum"))
        << "result splitter" << (optimizationResultsSplitter == nullptr
            ? -1 : optimizationResultsSplitter->count());
    return passed ? EXIT_SUCCESS : NECWB_SMOKE_FAILURE("final integrated smoke state");
}
