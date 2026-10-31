cmake_minimum_required(VERSION 3.21)

project(NecWorkbench VERSION 0.1.0 LANGUAGES CXX)

# Pre-release label shown to users (About box); leave empty for a final release.
set(NECWB_VERSION_LABEL "alpha.2")
if(NECWB_VERSION_LABEL)
    set(NECWB_DISPLAY_VERSION "${PROJECT_VERSION}-${NECWB_VERSION_LABEL}")
else()
    set(NECWB_DISPLAY_VERSION "${PROJECT_VERSION}")
endif()

# Single-configuration builds without a type compile unoptimized, which makes the GUI
# noticeably slower; default to an optimized build that keeps debug symbols.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(NECWB_BUILD_GUI "Build the Qt desktop application" ON)
option(NECWB_BUILD_TESTS "Build automated tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(necwb_core
    src/analysis/AdaptiveSearch.cpp
    src/analysis/AdaptiveSearch.h
    src/analysis/AnalysisResult.cpp
    src/analysis/AverageGainTest.cpp
    src/analysis/DifferentialEvolutionSearch.cpp
    src/analysis/DifferentialEvolutionSearch.h
    src/analysis/FrequencyPlan.cpp
    src/analysis/FrequencyComparison.h
    src/analysis/HistoricalOverlayCompatibility.cpp
    src/analysis/HistoricalOverlayCompatibility.h
    src/analysis/NecOutputParser.cpp
    src/analysis/NelderMeadSearch.cpp
    src/analysis/NelderMeadSearch.h
    src/analysis/OptimizationBaseline.cpp
    src/analysis/OptimizationBaseline.h
    src/analysis/OptimizationObjective.cpp
    src/analysis/OptimizationPreset.cpp
    src/analysis/OptimizationPreset.h
    src/analysis/ReferenceRunSelection.cpp
    src/analysis/ReferenceRunSelection.h
    src/analysis/ResultSummary.cpp
    src/analysis/SegmentCurrentDisplay.cpp
    src/analysis/SegmentCurrentDisplay.h
    src/analysis/ResultSummary.h
    src/analysis/SegmentationConvergence.cpp
    src/analysis/SolverCommand.cpp
    src/analysis/SolverInput.cpp
    src/geometry/OrthographicProjection.cpp
    src/model/AntennaModel.cpp
    src/model/SurfacePatch.h
    src/model/WireGeometry.cpp
    src/model/AutoSegmentation.cpp
    src/model/LengthUnit.cpp
    src/model/ModelSetup.cpp
    src/model/ModelSummary.cpp
    src/model/ModelSummary.h
    src/model/WireGauge.cpp
    src/nec/NecDocument.cpp
    src/nec/NecCardCatalog.cpp
    src/nec/NecCardCatalog.h
    src/nec/NecCardFieldEditor.cpp
    src/nec/NecCardFieldEditor.h
    src/nec/DeckGeometryUnits.cpp
    src/nec/NecModelChecker.cpp
    src/nec/NecModelConverter.cpp
    src/nec/NecParser.cpp
    src/nec/ParseNumber.h
    src/nec/NecSetupConverter.cpp
    src/nec/NecSourceEditor.cpp
    src/nec/NecSourceEditor.h
    src/nec/NecSourceCache.cpp
    src/nec/NecSourceCache.h
    src/nec/NecSymbolEditor.cpp
    src/nec/NecSymbolResolver.cpp
    src/nec/NecWriter.cpp
)

target_include_directories(necwb_core PUBLIC src)
target_compile_features(necwb_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(necwb_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(necwb_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NECWB_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()

    qt_add_library(necwb_ui STATIC
        src/ui/MainWindow.cpp
        src/ui/MainWindow.h
        src/ui/DetachablePanel.cpp
        src/ui/DetachablePanel.h
        src/ui/FileIo.h
        src/ui/ParameterFieldStyle.cpp
        src/ui/ParameterFieldStyle.h
        src/ui/WorkspaceNavigator.cpp
        src/ui/WorkspaceNavigator.h
        src/ui/analysis/AnalysisSetupEditor.cpp
        src/ui/analysis/AnalysisSetupEditor.h
        src/ui/analysis/AnalysisRequestEditor.cpp
        src/ui/analysis/AnalysisRequestEditor.h
        src/ui/analysis/AnalysisRunStore.cpp
        src/ui/analysis/AnalysisRunStore.h
        src/ui/analysis/AverageGainResultsView.cpp
        src/ui/analysis/AverageGainResultsView.h
        src/ui/analysis/ConvergenceWorkspace.cpp
        src/ui/analysis/ConvergenceWorkspace.h
        src/ui/analysis/HistoricalOverlayControl.cpp
        src/ui/analysis/HistoricalOverlayControl.h
        src/ui/analysis/ImpedanceResultsView.cpp
        src/ui/analysis/ImpedanceResultsView.h
        src/ui/analysis/QuickSweepDialog.cpp
        src/ui/analysis/QuickSweepDialog.h
        src/ui/analysis/ResultsSummaryView.cpp
        src/ui/analysis/ResultsSummaryView.h
        src/ui/analysis/ResultReferenceController.cpp
        src/ui/analysis/ResultReferenceController.h
        src/ui/analysis/ResultWindowCoordinator.cpp
        src/ui/analysis/ResultWindowCoordinator.h
        src/ui/analysis/RunReviewWindow.cpp
        src/ui/analysis/RunReviewWindow.h
        src/ui/analysis/SolverProcessRunner.cpp
        src/ui/analysis/SolverProcessRunner.h
        src/ui/analysis/SweepPlotsView.cpp
        src/ui/analysis/SweepPlotsView.h
        src/ui/analysis/CurrentAntennaView.cpp
        src/ui/analysis/CurrentAntennaView.h
        src/ui/analysis/FieldResultsViews.cpp
        src/ui/analysis/FieldResultsViews.h
        src/ui/cards/CompactNumberDelegate.cpp
        src/ui/cards/CompactNumberDelegate.h
        src/ui/cards/WireCardEditor.cpp
        src/ui/cards/WireCardEditor.h
        src/ui/cards/StructuredCardEditor.cpp
        src/ui/cards/StructuredCardEditor.h
        src/ui/controlpanel/ControlPanel.cpp
        src/ui/controlpanel/ControlPanel.h
        src/ui/editor/NecEditor.cpp
        src/ui/editor/NecEditor.h
        src/ui/editor/NecHighlighter.cpp
        src/ui/editor/NecHighlighter.h
        src/ui/geometry/GeometryView.cpp
        src/ui/geometry/GeometryView.h
        src/ui/geometry/GeometryInteractionView.cpp
        src/ui/geometry/GeometryInteractionView.h
        src/ui/geometry/GeometrySettings.h
        src/ui/geometry/EngineeringSpinBox.cpp
        src/ui/geometry/EngineeringSpinBox.h
        src/ui/geometry/AutoSegmentationDialog.cpp
        src/ui/geometry/AutoSegmentationDialog.h
        src/ui/geometry/Geometry3DView.cpp
        src/ui/geometry/Geometry3DView.h
        src/ui/geometry/GeometrySettingsDialog.cpp
        src/ui/geometry/GeometrySettingsDialog.h
        src/ui/geometry/WirePropertiesDialog.cpp
        src/ui/geometry/WirePropertiesDialog.h
        src/ui/optimization/OptimizationWorkspace.cpp
        src/ui/optimization/OptimizationWorkspace.h
        src/ui/optimization/OptimizationWorkspaceCandidates.cpp
        src/ui/optimization/OptimizationWorkspaceFrequencies.cpp
        src/ui/optimization/OptimizationWorkspaceLayout.cpp
        src/ui/optimization/OptimizationWorkspaceObjective.cpp
        src/ui/optimization/OptimizationWorkspaceSession.cpp
        src/ui/optimization/OptimizationWorkspaceSupport.cpp
        src/ui/optimization/OptimizationWorkspaceSupport.h
        src/ui/optimization/OptimizationComparePanel.cpp
        src/ui/optimization/OptimizationComparePanel.h
        src/ui/optimization/CandidateEvaluator.cpp
        src/ui/optimization/CandidateEvaluator.h
        src/ui/optimization/DirectionStrategyDialog.cpp
        src/ui/optimization/DirectionStrategyDialog.h
        src/ui/model/ParameterEditor.cpp
        src/ui/model/ParameterEditor.h
        src/ui/setup/SetupEditor.cpp
        src/ui/setup/SetupEditor.h
        src/ui/setup/LoadNetworkEditor.cpp
        src/ui/setup/LoadNetworkEditor.h
        src/ui/setup/ExcitationPropertiesDialog.cpp
        src/ui/setup/ExcitationPropertiesDialog.h
        src/ui/welcome/WelcomePage.cpp
        src/ui/welcome/WelcomePage.h
    )
    qt_add_resources(necwb_ui necwb_icons
        PREFIX "/icons"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
        FILES
            resources/icons/3d-view.svg
            resources/icons/analysis.svg
            resources/icons/check.svg
            resources/icons/currents.svg
            resources/icons/delete.svg
            resources/icons/detach.svg
            resources/icons/edit.svg
            resources/icons/environment.svg
            resources/icons/export.svg
            resources/icons/fit.svg
            resources/icons/frequency-sweep.svg
            resources/icons/geometry.svg
            resources/icons/ground.svg
            resources/icons/help.svg
            resources/icons/import.svg
            resources/icons/loads.svg
            resources/icons/nec-deck.svg
            resources/icons/new.svg
            resources/icons/open.svg
            resources/icons/optimize.svg
            resources/icons/quick-sweep.svg
            resources/icons/radiation-pattern.svg
            resources/icons/redo.svg
            resources/icons/results.svg
            resources/icons/requests.svg
            resources/icons/run.svg
            resources/icons/runs.svg
            resources/icons/save.svg
            resources/icons/settings.svg
            resources/icons/sources.svg
            resources/icons/stop.svg
            resources/icons/structured-cards.svg
            resources/icons/transmission-lines.svg
            resources/icons/undo.svg
            resources/icons/validate.svg
            resources/icons/variables.svg
    )
    target_link_libraries(necwb_ui PUBLIC necwb_core Qt6::Widgets)
    target_compile_definitions(necwb_ui PUBLIC NECWB_DISPLAY_VERSION="${NECWB_DISPLAY_VERSION}")
    if(MSVC)
        target_compile_options(necwb_ui PRIVATE /W4 /permissive-)
    else()
        target_compile_options(necwb_ui PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    qt_add_executable(nec-workbench src/app/main.cpp)
    target_link_libraries(nec-workbench PRIVATE necwb_ui)
    if(WIN32)
        set_target_properties(nec-workbench PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    install(TARGETS nec-workbench
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
    install(FILES LICENSE
        DESTINATION share/licenses/nec-workbench
    )

    if(UNIX AND NOT APPLE)
        install(FILES packaging/nec-workbench.desktop
            DESTINATION share/applications
        )
        install(FILES packaging/nec-workbench.svg
            DESTINATION share/icons/hicolor/scalable/apps
        )
    endif()
endif()

if(NECWB_BUILD_TESTS)
    enable_testing()
    add_executable(necwb_core_tests tests/CoreTests.cpp)
    target_link_libraries(necwb_core_tests PRIVATE necwb_core)
    add_test(NAME necwb_core_tests COMMAND necwb_core_tests)

    if(NECWB_BUILD_GUI)
        add_executable(necwb_plot_tests tests/PlotSmokeTests.cpp)
        target_link_libraries(necwb_plot_tests PRIVATE necwb_ui)
        add_test(NAME necwb_plot_tests COMMAND necwb_plot_tests)
        set_tests_properties(necwb_plot_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(necwb_optimization_tests tests/OptimizationEvaluatorTests.cpp)
        target_link_libraries(necwb_optimization_tests PRIVATE necwb_ui)
        add_test(NAME necwb_optimization_tests COMMAND necwb_optimization_tests)
    endif()
endif()
