cmake_minimum_required(VERSION 3.21)

project(NecWorkbench VERSION 0.1.0 LANGUAGES CXX)

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
    src/analysis/FrequencyPlan.cpp
    src/analysis/NecOutputParser.cpp
    src/analysis/NelderMeadSearch.cpp
    src/analysis/NelderMeadSearch.h
    src/analysis/OptimizationObjective.cpp
    src/analysis/SegmentationConvergence.cpp
    src/analysis/SolverCommand.cpp
    src/analysis/SolverInput.cpp
    src/geometry/OrthographicProjection.cpp
    src/model/AntennaModel.cpp
    src/model/WireGeometry.cpp
    src/model/AutoSegmentation.cpp
    src/model/LengthUnit.cpp
    src/model/ModelSetup.cpp
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
    src/nec/NecSetupConverter.cpp
    src/nec/NecSourceEditor.cpp
    src/nec/NecSourceEditor.h
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
        src/ui/ParameterFieldStyle.cpp
        src/ui/ParameterFieldStyle.h
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
        src/ui/analysis/ImpedanceResultsView.cpp
        src/ui/analysis/ImpedanceResultsView.h
        src/ui/analysis/QuickSweepDialog.cpp
        src/ui/analysis/QuickSweepDialog.h
        src/ui/analysis/ResultsSummaryView.cpp
        src/ui/analysis/ResultsSummaryView.h
        src/ui/analysis/RunReviewWindow.cpp
        src/ui/analysis/RunReviewWindow.h
        src/ui/analysis/SolverProcessRunner.cpp
        src/ui/analysis/SolverProcessRunner.h
        src/ui/analysis/SweepPlotsView.cpp
        src/ui/analysis/SweepPlotsView.h
        src/ui/analysis/FieldResultsViews.cpp
        src/ui/analysis/FieldResultsViews.h
        src/ui/cards/WireCardEditor.cpp
        src/ui/cards/WireCardEditor.h
        src/ui/cards/StructuredCardEditor.cpp
        src/ui/cards/StructuredCardEditor.h
        src/ui/commands/GeometrySourceCommand.cpp
        src/ui/commands/GeometrySourceCommand.h
        src/ui/commands/MoveEndpointCommand.cpp
        src/ui/commands/MoveEndpointCommand.h
        src/ui/commands/MoveWireCommand.cpp
        src/ui/commands/MoveWireCommand.h
        src/ui/dashboard/DashboardPage.cpp
        src/ui/dashboard/DashboardPage.h
        src/ui/editor/NecEditor.cpp
        src/ui/editor/NecEditor.h
        src/ui/editor/NecHighlighter.cpp
        src/ui/editor/NecHighlighter.h
        src/ui/geometry/GeometryView.cpp
        src/ui/geometry/GeometryView.h
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
        src/ui/optimization/CandidateEvaluator.cpp
        src/ui/optimization/CandidateEvaluator.h
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
            resources/icons/delete.svg
            resources/icons/detach.svg
            resources/icons/edit.svg
            resources/icons/environment.svg
            resources/icons/export.svg
            resources/icons/fit.svg
            resources/icons/frequency-sweep.svg
            resources/icons/geometry.svg
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
            resources/icons/run.svg
            resources/icons/save.svg
            resources/icons/settings.svg
            resources/icons/sources.svg
            resources/icons/stop.svg
            resources/icons/structured-cards.svg
            resources/icons/transmission-lines.svg
            resources/icons/undo.svg
            resources/icons/validate.svg
    )
    target_link_libraries(necwb_ui PUBLIC necwb_core Qt6::Widgets)
    if(MSVC)
        target_compile_options(necwb_ui PRIVATE /W4 /permissive-)
    else()
        target_compile_options(necwb_ui PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    qt_add_executable(nec-workbench src/app/main.cpp)
    target_link_libraries(nec-workbench PRIVATE necwb_ui)

    install(TARGETS nec-workbench
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
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
