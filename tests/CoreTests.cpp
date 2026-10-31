#include "analysis/SegmentCurrentDisplay.h"
#include "analysis/SolverCommand.h"
#include "analysis/AdaptiveSearch.h"
#include "analysis/AverageGainTest.h"
#include "analysis/DifferentialEvolutionSearch.h"
#include "analysis/FrequencyPlan.h"
#include "analysis/HistoricalOverlayCompatibility.h"
#include "analysis/NecOutputParser.h"
#include "analysis/NelderMeadSearch.h"
#include "analysis/OptimizationBaseline.h"
#include "analysis/OptimizationObjective.h"
#include "analysis/OptimizationPreset.h"
#include "analysis/ReferenceRunSelection.h"
#include "analysis/ResultSummary.h"
#include "analysis/SegmentationConvergence.h"
#include "analysis/SolverInput.h"
#include "geometry/OrthographicProjection.h"
#include "model/AutoSegmentation.h"
#include "model/LengthUnit.h"
#include "model/ModelSummary.h"
#include "model/WireGeometry.h"
#include "model/WireGauge.h"
#include "nec/DeckGeometryUnits.h"
#include "nec/NecCardCatalog.h"
#include "nec/NecCardFieldEditor.h"
#include "nec/NecModelConverter.h"
#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"
#include "nec/NecSourceEditor.h"
#include "nec/NecSourceCache.h"
#include "nec/NecSymbolResolver.h"
#include "nec/NecSymbolEditor.h"
#include "nec/NecWriter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

auto failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testRoundTripPreservesSource()
{
    const std::string source = "CM dipole\r\nXX 1   unsupported\r\n\r\nEN\r\n";
    const auto document = necwb::nec::NecParser{}.parse(source);
    expect(document.cards().size() == 4, "parser retains every source line");
    expect(document.cards()[1].kind == necwb::nec::NecCardKind::Unknown, "unknown card is represented");
    expect(document.cards()[2].kind == necwb::nec::NecCardKind::Blank, "blank line is represented");
    expect(necwb::nec::NecWriter{}.write(document) == source, "writer preserves CRLF source exactly");
}

void testReferenceRunSelection()
{
    using necwb::analysis::ReferenceRunCandidate;
    const std::vector<ReferenceRunCandidate> candidates{
        {"older", "/models/dipole.nec", "analysis", "Completed", 10},
        {"newer", "/models/dipole.nec", "quick-sweep", "Completed", 30},
        {"active", "/models/dipole.nec", "analysis", "Completed", 40},
        {"failed", "/models/dipole.nec", "analysis", "Failed", 50},
        {"optimization", "/models/dipole.nec", "optimization-session", "Completed", 60},
        {"other-model", "/models/yagi.nec", "analysis", "Completed", 70},
    };
    const auto selected = necwb::analysis::selectPreviousReferenceRun(
        candidates, "active", "/models/dipole.nec");
    expect(selected && candidates[*selected].id == "newer",
        "reference selection chooses the newest eligible previous run");
    expect(!necwb::analysis::selectPreviousReferenceRun(candidates, "active", {}),
        "untitled models do not receive an automatic reference");
    expect(!necwb::analysis::selectPreviousReferenceRun(
        candidates, "active", "/models/missing.nec"),
        "reference selection does not cross model identities");
}

void testHistoricalOverlayCompatibility()
{
    using necwb::analysis::AnalysisResult;
    using necwb::analysis::FeedpointResult;
    using necwb::analysis::HistoricalOverlayStatus;
    using necwb::analysis::RadiationSample;

    AnalysisResult current;
    current.feedpoints = {
        FeedpointResult{14.0, 1, 6},
        FeedpointResult{14.1, 1, 6},
    };
    current.radiation = {
        RadiationSample{14.0, 90.0, 0.0, 0.0, 0.0, 5.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 1},
        RadiationSample{14.0, 90.0, 180.0, 0.0, 0.0, -5.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 1},
        RadiationSample{14.1, 90.0, 0.0, 0.0, 0.0, 5.1, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 2},
        RadiationSample{14.1, 90.0, 180.0, 0.0, 0.0, -5.1, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 2},
    };
    auto historical = current;
    auto compatibility = necwb::analysis::evaluateHistoricalOverlayCompatibility(
        current, historical);
    expect(compatibility.impedance == HistoricalOverlayStatus::Compatible
            && compatibility.radiation == HistoricalOverlayStatus::Compatible,
        "historical overlay recognizes fully compatible results");

    historical.feedpoints.pop_back();
    historical.radiation.resize(2);
    compatibility = necwb::analysis::evaluateHistoricalOverlayCompatibility(
        current, historical);
    expect(compatibility.impedance == HistoricalOverlayStatus::Partial
            && compatibility.radiation == HistoricalOverlayStatus::Partial,
        "historical overlay recognizes partial result coverage");

    historical = current;
    historical.referenceImpedanceOhms = 75.0;
    compatibility = necwb::analysis::evaluateHistoricalOverlayCompatibility(
        current, historical);
    expect(compatibility.impedance == HistoricalOverlayStatus::Partial,
        "historical overlay reports differing reference impedance as partial");

    for (auto& sample : historical.feedpoints) sample.frequencyMHz += 10.0;
    for (auto& sample : historical.radiation) sample.frequencyMHz += 10.0;
    compatibility = necwb::analysis::evaluateHistoricalOverlayCompatibility(
        current, historical);
    expect(compatibility.impedance == HistoricalOverlayStatus::NoMatchingData
            && compatibility.radiation == HistoricalOverlayStatus::NoMatchingData,
        "historical overlay rejects unrelated result grids");
}

void testControlPanelSummaries()
{
    necwb::model::AntennaModel model;
    model.addWire({1, {-5.0, 0.0, 10.0}, {0.0, 0.0, 10.0}, 11, 0.001, 3});
    model.addWire({2, {0.0, 0.0, 10.0}, {5.0, 0.0, 10.0}, 9, 0.001, 4});
    necwb::model::ModelSetup setup;
    setup.frequency = necwb::model::FrequencyDefinition{0, 3, 14.0, 0.1, 6};
    setup.ground = necwb::model::GroundDefinition{
        necwb::model::GroundType::SommerfeldNorton};
    setup.executionRequest = necwb::model::ExecutionRequest{0, 11};
    setup.excitations = {{0, 2, 5, 1.0, 0.0, 7}};
    setup.loads = {{0, 1, 1, 11, 50.0, 0.0, 0.0, 8}};
    setup.transmissionLines = {{1, 11, 2, 1, 50.0, 2.0, 0.0, 0.0, 0.0, 0.0, 9}};
    setup.radiationPatterns = {{19, 37, 0.0, 0.0, 5.0, 10.0, 10}};
    const auto document = necwb::nec::NecParser{}.parse(
        "CM summary model\nSY half=5\nGW 1 11 -5 0 10 0 0 10 .001\n"
        "GW 2 9 0 0 10 5 0 10 .001\nGE 0\nFR 0 3 0 0 14 .1\n"
        "EX 0 2 5 0 1 0\nLD 0 1 1 11 50 0 0\nTL 1 11 2 1 50 2\n"
        "RP 0 19 37 1000 0 0 5 10\nXQ\nEN\n\n");
    const auto modelSummary = necwb::model::summarizeModel(model, setup, document);
    expect(modelSummary.wireCount == 2 && modelSummary.segmentCount == 20
            && modelSummary.sources.size() == 1 && modelSummary.sources[0].wireTag == 2
            && modelSummary.sources[0].segment == 5 && modelSummary.loadCount == 1
            && modelSummary.transmissionLineCount == 1,
        "model summary counts geometry and retains source location");
    expect(modelSummary.frequency && modelSummary.frequency->startMHz == 14.0
            && modelSummary.frequency->endMHz == 14.2
            && modelSummary.frequency->pointCount == 3,
        "model summary retains the authored frequency sweep");
    expect(modelSummary.groundType == necwb::model::GroundType::SommerfeldNorton
            && modelSummary.symbolCount == 1
            && modelSummary.radiationRequests.size() == 1
            && modelSummary.radiationRequests[0].thetaCount == 19
            && modelSummary.radiationRequests[0].phiCount == 37
            && modelSummary.executionRequestCount == 1
            && modelSummary.cardCount == 12,
        "model summary counts variables, requests, and nonblank deck cards");

    const auto emptySummary = necwb::analysis::summarizeResults({}, 14.0);
    expect(!emptySummary.selectedFeedpoint && !emptySummary.minimumSwr
            && !emptySummary.peakGain,
        "result summary represents an empty analysis without placeholder values");

    necwb::analysis::AnalysisResult singleFrequency;
    singleFrequency.feedpoints = {{7.1, 1, 11, {}, {}, {50.0, 0.0}, 0.5}};
    singleFrequency.radiation = {
        {7.1, 0.0, 0.0, 0.0, 0.0, -5.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 0},
        {7.1, 90.0, 0.0, 0.0, 0.0, 6.2, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 0},
    };
    const auto singleSummary = necwb::analysis::summarizeResults(singleFrequency, 7.1);
    expect(singleSummary.selectedFeedpoint && singleSummary.minimumSwr
            && singleSummary.selectedFeedpoint->frequencyMHz == 7.1
            && singleSummary.selectedFeedpoint->swr == 1.0
            && singleSummary.minimumSwr->frequencyMHz == 7.1,
        "result summary reports one-frequency impedance and minimum SWR consistently");
    expect(singleSummary.peakGain && singleSummary.peakGain->frequencyMHz == 7.1
            && singleSummary.peakGain->gainDb == 6.2
            && singleSummary.peakGain->datasetKind
                == necwb::analysis::RadiationDatasetKind::ElevationCut,
        "result summary identifies peak gain and its elevation cut");
    expect(singleSummary.selectedGain && singleSummary.selectedGain->frequencyMHz == 7.1
            && singleSummary.selectedGain->gainDb == 6.2,
        "selected-frequency gain matches the radiation peak at that frequency");

    necwb::analysis::AnalysisResult sweep;
    sweep.feedpoints = {
        {14.0, 1, 6, {}, {}, {50.0, 0.0}},
        {14.2, 1, 6, {}, {}, {75.0, 0.0}},
        {14.0, 2, 6, {}, {}, {150.0, 0.0}},
        {14.2, 2, 6, {}, {}, {100.0, 0.0}},
    };
    sweep.radiation = {
        {14.0, 90.0, 0.0, 0.0, 0.0, 3.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 2},
        {14.2, 90.0, 0.0, 0.0, 0.0, 4.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 3},
        {21.0, 45.0, 0.0, 0.0, 0.0, 5.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 4},
        {21.0, 45.0, 90.0, 0.0, 0.0, 6.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 4},
        {21.0, 90.0, 0.0, 0.0, 0.0, 7.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 4},
        {21.0, 90.0, 90.0, 0.0, 0.0, 8.0, 0.0, 0.0,
            necwb::analysis::PolarizationSense::Unknown, 4},
    };
    const auto sweepSummary = necwb::analysis::summarizeResults(
        sweep, 14.19, necwb::analysis::FeedpointIdentity{2, 6});
    expect(sweepSummary.selectedFeedpoint && sweepSummary.minimumSwr
            && sweepSummary.selectedFeedpoint->frequencyMHz == 14.2
            && sweepSummary.selectedFeedpoint->source
                == necwb::analysis::FeedpointIdentity{2, 6}
            && sweepSummary.minimumSwr->frequencyMHz == 14.2
            && sweepSummary.minimumSwr->source
                == necwb::analysis::FeedpointIdentity{2, 6},
        "result summary keeps selected and minimum SWR values on one source");
    expect(sweepSummary.peakGain && sweepSummary.peakGain->frequencyMHz == 21.0
            && sweepSummary.peakGain->gainDb == 8.0
            && sweepSummary.peakGain->patternIndex == 4
            && sweepSummary.peakGain->datasetKind
                == necwb::analysis::RadiationDatasetKind::FullPattern,
        "result summary labels a peak from another frequency and full pattern dataset");
    expect(!sweepSummary.selectedGain, "selected gain hides a point sample");

    necwb::analysis::AnalysisResult mixedPattern;
    mixedPattern.radiation = {
        {14.0, 0.0, 0.0, 0.0, 0.0, -5.0, 0.0, 0.0, {}, 0},
        {14.0, 90.0, 0.0, 0.0, 0.0, 3.1, 0.0, 0.0, {}, 0},
        {14.35, 55.0, 0.0, 0.0, 0.0, 6.07, 0.0, 0.0, {}, 1},
    };
    const auto fullPatternSummary = necwb::analysis::summarizeResults(mixedPattern, 14.0),
        pointSummary = necwb::analysis::summarizeResults(mixedPattern, 14.35);
    expect(fullPatternSummary.selectedGain && fullPatternSummary.selectedGain->gainDb == 3.1,
        "multi-angle selected gain");
    expect(!pointSummary.selectedGain, "point sample is not a pattern peak");
}

void testOptimizationBaselineFingerprint()
{
    const std::string first =
        "CM first formatting\n"
        "GW 1 11 0 0 0 1.0 0 0 .001\n"
        "GE 0\n"
        "EX 0 1 6 0 1 0\n"
        "FR 0 3 0 0 7.0 .1\n"
        "RP 0 1 1 1000 90 0 0 0\n"
        "EN\n";
    const std::string reformatted =
        "CM different comment\n"
        "GW,1,11,0,0,0,1e0,0,0,1e-3\n"
        "GE 0\n"
        "EX 0 1 6 0 1 0\n"
        "FR 0 1 0 0 14.2 0\n"
        "XQ 0\n"
        "EN\n";
    const std::string changed =
        "GW 1 11 0 0 0 1.1 0 0 .001\n"
        "GE 0\n"
        "EX 0 1 6 0 1 0\n"
        "EN\n";
    const auto fingerprint = necwb::analysis::optimizationModelFingerprint(first);
    expect(!fingerprint.empty() && fingerprint.size() == 32,
        "optimization model fingerprints are stable identifiers");
    expect(fingerprint == necwb::analysis::optimizationModelFingerprint(reformatted),
        "optimization fingerprints ignore comments, formatting, and execution requests");
    expect(fingerprint != necwb::analysis::optimizationModelFingerprint(changed),
        "optimization fingerprints change with solver-effective model values");

    const auto firstResolution = necwb::nec::NecSymbolResolver{}.resolve(
        "SY length=1\nGW 1 11 0 0 0 length 0 0 .001\nGE 0\nEN\n");
    const auto secondResolution = necwb::nec::NecSymbolResolver{}.resolve(
        "SY length=1.1\nGW 1 11 0 0 0 length 0 0 .001\nGE 0\nEN\n");
    expect(firstResolution.ok() && secondResolution.ok()
            && necwb::analysis::optimizationModelFingerprint(firstResolution.generatedDeck)
                != necwb::analysis::optimizationModelFingerprint(secondResolution.generatedDeck),
        "optimization fingerprints include resolved SY values");

    // Solver decks omit the Workbench reference-impedance card, so an analysis of the same
    // model must still match the study.
    const std::string withReference =
        "ZO 75\nGW 1 11 0 0 0 1 0 0 .001\nGE 0\nEX 0 1 6 0 1 0\nFR 0 1 0 0 14.2 0\nEN\n";
    expect(necwb::analysis::optimizationModelFingerprint(withReference)
            == necwb::analysis::optimizationModelFingerprint(
                necwb::analysis::prepareImpedanceInput(withReference)),
        "optimization fingerprints ignore the reference-impedance card removed from solver decks");
}

void testOptimizationBaselineCompatibility()
{
    necwb::analysis::AnalysisResult result;
    result.referenceImpedanceOhms = 50.0;
    result.feedpoints = {
        {.frequencyMHz = 14.0},
        {.frequencyMHz = 14.2},
    };
    result.radiation = {
        {.frequencyMHz = 14.0, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .patternIndex = 1},
        {.frequencyMHz = 14.0, .thetaDegrees = 90.0, .phiDegrees = 5.0,
            .patternIndex = 1},
    };
    const auto coverage = necwb::analysis::optimizationResultCoverage(result);
    expect(coverage.impedanceFrequenciesMHz.size() == 2
            && coverage.radiation.size() == 1
            && coverage.radiation.front().sampleCount == 2,
        "optimization result coverage indexes impedance and radiation grids");

    const necwb::analysis::OptimizationBaselineDescriptor baseline{
        "model-a", "nec2c", coverage};
    const necwb::analysis::OptimizationBaselineRequirements requirements{
        "model-a", "nec2c", 50.0, {14.0, 14.2}, {14.0}, coverage.radiation};
    expect(necwb::analysis::evaluateOptimizationBaseline(baseline, requirements).compatible(),
        "matching optimization baseline metadata is compatible");
    expect(necwb::analysis::evaluateOptimizationBaseline(std::nullopt, requirements).status
            == necwb::analysis::OptimizationBaselineStatus::Missing,
        "missing optimization baseline is reported");

    auto changedModel = requirements;
    changedModel.modelFingerprint = "model-b";
    expect(necwb::analysis::evaluateOptimizationBaseline(baseline, changedModel).status
            == necwb::analysis::OptimizationBaselineStatus::ModelChanged,
        "changed model invalidates an optimization baseline");
    auto changedBackend = requirements;
    changedBackend.backend = "opennec";
    expect(necwb::analysis::evaluateOptimizationBaseline(baseline, changedBackend).status
            == necwb::analysis::OptimizationBaselineStatus::BackendChanged,
        "changed solver backend invalidates an optimization baseline");
    auto changedImpedance = requirements;
    changedImpedance.referenceImpedanceOhms = 75.0;
    expect(necwb::analysis::evaluateOptimizationBaseline(baseline, changedImpedance).status
            == necwb::analysis::OptimizationBaselineStatus::ReferenceImpedanceChanged,
        "changed reference impedance invalidates SWR baseline coverage");
    auto missingFrequency = requirements;
    missingFrequency.impedanceFrequenciesMHz.push_back(14.3);
    const auto frequencyCompatibility = necwb::analysis::evaluateOptimizationBaseline(
        baseline, missingFrequency);
    expect(frequencyCompatibility.status
            == necwb::analysis::OptimizationBaselineStatus::MissingImpedanceCoverage
            && frequencyCompatibility.missingImpedanceFrequenciesMHz.size() == 1,
        "missing impedance frequency is reported explicitly");
    auto missingRadiationFrequency = requirements;
    missingRadiationFrequency.radiationFrequenciesMHz.push_back(14.2);
    const auto radiationFrequencyCompatibility =
        necwb::analysis::evaluateOptimizationBaseline(
            baseline, missingRadiationFrequency);
    expect(radiationFrequencyCompatibility.status
            == necwb::analysis::OptimizationBaselineStatus::MissingRadiationCoverage
            && radiationFrequencyCompatibility.missingRadiationFrequenciesMHz.size() == 1,
        "missing radiation frequency is reported explicitly");
    auto missingPattern = requirements;
    missingPattern.radiation.front().directionFingerprint = "different-grid";
    const auto patternCompatibility = necwb::analysis::evaluateOptimizationBaseline(
        baseline, missingPattern);
    expect(patternCompatibility.status
            == necwb::analysis::OptimizationBaselineStatus::MissingRadiationCoverage
            && patternCompatibility.missingRadiation.size() == 1
            && patternCompatibility.missingRadiationFrequenciesMHz.empty(),
        "mismatched radiation grid invalidates pattern baseline coverage");

    std::ranges::reverse(result.radiation);
    expect(necwb::analysis::optimizationResultCoverage(result).radiation == coverage.radiation,
        "radiation coverage fingerprints are independent of solver sample order");
}

void testOptimizationPresets()
{
    using namespace necwb::analysis;

    const auto definitions = optimizationPresetDefinitions();
    expect(definitions.size() == 8, "optimization preset catalog includes seven goals and Custom");
    for (auto index = std::size_t{}; index < definitions.size(); ++index) {
        const auto& definition = definitions[index];
        expect(!definition.stableId.empty(), "optimization preset has a stable ID");
        expect(optimizationPresetStableId(definition.id) == definition.stableId,
            "optimization preset ID resolves to its stable ID");
        for (auto other = index + 1; other < definitions.size(); ++other)
            expect(definition.stableId != definitions[other].stableId,
                "optimization preset stable IDs are unique");
    }
    expect(!optimizationPresetObjective(OptimizationPresetId::Custom),
        "Custom does not replace the current objective");

    const OptimizationPresetParameters parameters{75.0, 1.8};
    const auto minimizeSwr = optimizationPresetObjective(
        OptimizationPresetId::MinimizeSwr, parameters);
    expect(minimizeSwr && minimizeSwr->referenceImpedance == 75.0
            && minimizeSwr->swrWeight == 1.0
            && minimizeSwr->swrGoal == OptimizationGoal::Minimize
            && minimizeSwr->swrAggregation == OptimizationAggregation::Maximum
            && minimizeSwr->resistanceWeight == 0.0
            && minimizeSwr->forwardGainWeight == 0.0,
        "minimize SWR preset continuously rewards lower worst-band SWR");

    const auto swr = optimizationPresetObjective(OptimizationPresetId::SwrLimit, parameters);
    expect(swr && swr->referenceImpedance == 75.0 && swr->swrWeight == 1.0
            && swr->swrGoal == OptimizationGoal::GoodEnough && swr->swrTarget == 1.8
            && swr->swrGoodEnoughDirection == GoodEnoughDirection::AtMost
            && swr->swrAggregation == OptimizationAggregation::Maximum
            && swr->resistanceWeight == 0.0 && swr->forwardGainWeight == 0.0,
        "SWR limit preset configures only worst-band SWR compliance");

    const auto match = optimizationPresetObjective(
        OptimizationPresetId::FeedpointMatch, parameters);
    expect(match && match->swrWeight == 0.0 && match->resistanceWeight == 1.0
            && match->resistanceGoal == OptimizationGoal::Target
            && match->resistanceTargetOhms == 75.0
            && match->resistanceAggregation == OptimizationAggregation::Maximum
            && match->reactanceWeight == 1.0
            && match->reactanceGoal == OptimizationGoal::Target
            && match->reactanceTargetOhms == 0.0
            && match->reactanceAggregation == OptimizationAggregation::Maximum,
        "feedpoint match preset targets reference resistance and zero reactance");

    const auto gain = optimizationPresetObjective(OptimizationPresetId::PeakGain, parameters);
    expect(gain && gain->forwardGainWeight == 1.0
            && gain->forwardGainGoal == OptimizationGoal::Maximize
            && gain->forwardGainAggregation == OptimizationAggregation::Minimum
            && gain->directionalEvaluationMode == DirectionalEvaluationMode::StrongestInAzimuthCut,
        "peak gain preset maximizes the weakest frequency in a tracked cut");

    const auto frontToBack = optimizationPresetObjective(
        OptimizationPresetId::FrontToBack, parameters);
    expect(frontToBack && frontToBack->frontToBackWeight == 1.0
            && frontToBack->frontToBackGoal == OptimizationGoal::Maximize
            && frontToBack->frontToBackAggregation == OptimizationAggregation::Minimum,
        "front-to-back preset maximizes the weakest frequency");

    const auto frontToRear = optimizationPresetObjective(
        OptimizationPresetId::FrontToRear, parameters);
    expect(frontToRear && frontToRear->frontToRearWeight == 1.0
            && frontToRear->frontToRearGoal == OptimizationGoal::Maximize
            && frontToRear->frontToRearAggregation == OptimizationAggregation::Minimum,
        "front-to-rear preset maximizes the weakest frequency");

    const auto combined = optimizationPresetObjective(
        OptimizationPresetId::SwrAndGain, parameters);
    expect(combined && combined->swrWeight == 1.0
            && combined->swrGoal == OptimizationGoal::GoodEnough
            && combined->swrTarget == 1.8 && combined->forwardGainWeight == 1.0
            && combined->forwardGainGoal == OptimizationGoal::Maximize
            && combined->forwardGainAggregation == OptimizationAggregation::Minimum,
        "combined preset balances SWR compliance and weakest-frequency gain");

    for (const auto& definition : definitions) {
        if (definition.id == OptimizationPresetId::Custom) continue;
        const auto objective = optimizationPresetObjective(definition.id, parameters);
        const auto directional = objective && (objective->forwardGainWeight > 0.0
            || objective->frontToBackWeight > 0.0 || objective->frontToRearWeight > 0.0);
        expect(directional == definition.requiresDirectionalSetup,
            "preset directional requirement matches enabled criteria");
    }
}

void testUtf8BomParsing()
{
    const std::string source = "\xEF\xBB\xBFGW 1 3 0 0 0 1 0 0 .001\nGE 0\nEN\n";
    const auto document = necwb::nec::NecParser{}.parse(source);
    expect(document.cards().size() == 3
            && document.cards()[0].kind == necwb::nec::NecCardKind::GeometryWire
            && document.cards()[0].mnemonic == "GW",
        "UTF-8 BOM does not hide the first NEC card mnemonic");
    expect(document.cards()[0].fields.size() == 9
            && document.cards()[0].fields[0] == "1",
        "UTF-8 BOM does not alter first-card fields");
    expect(necwb::nec::NecWriter{}.write(document) == source,
        "UTF-8 BOM is preserved by source round trips");
}

void testLegacyFreeFormatParsing()
{
    const std::string source =
        "CM Dipolo per i 40m  ( i2kfx )\n"
        "CM\n"
        "CM\n"
        "CMPP  1 0 0 0 0\n"
        "CE\n"
        "GW 1 21  0 -10.64 0  0  +10.64  0 0.004\n"
        "GE 0\n"
        "FR 0 0 0 0 7.05\n"
        "EX 0 1 11 0 1.0 0.0\n"
        "PT -1\n"
        "RP  0, 1, 1,  1001, 90, 0, 0, 0\n"
        "RP  0, 1, 1,  1001, 90,180, 0, 0\n"
        "RP  0, 1, 361, 1001, 90, 0, 1, 1\n"
        "RP  0, 361, 1, 1001, 0, 0, 1, 1\n"
        "EN\n";
    const auto document = necwb::nec::NecParser{}.parse(source);
    const auto setup = necwb::nec::NecSetupConverter{}.convert(document);
    const auto result = necwb::nec::NecModelChecker{}.check(document);
    expect(document.cards().size() == 15
            && document.cards()[3].kind == necwb::nec::NecCardKind::Comment,
        "NEC card mnemonics use their first two characters");
    expect(document.cards()[10].fields.size() == 8
            && document.cards()[11].fields.size() == 8,
        "free-format NEC fields accept mixed comma and whitespace separators");
    expect(result.errorCount() == 0 && result.model.wireCount() == 1,
        "legacy signed and comma-separated NEC deck passes model checking");
    expect(setup.frequency && setup.frequency->count == 1
            && setup.frequency->startMHz == 7.05,
        "zero FR frequency count uses the NEC-2 single-frequency default");
    expect(setup.radiationPatterns.size() == 4,
        "all comma-separated RP requests convert to structured requests");
    expect(necwb::nec::NecWriter{}.write(document) == source,
        "legacy free-format parsing preserves authored source exactly");
}

void testSourceLineEditing()
{
    const std::string source = "CM note\nGW 1 3 0 0 0 1 0 0 .001\nEN\n";
    const auto replaced = necwb::nec::replaceSourceLine(source, 2, "GW replacement");
    expect(replaced && *replaced == "CM note\nGW replacement\nEN\n",
        "source-line replacement preserves line structure and trailing newline");
    const auto inserted = necwb::nec::insertSourceLine(source, 2, "GE 0");
    expect(inserted && *inserted == "CM note\nGW 1 3 0 0 0 1 0 0 .001\nGE 0\nEN\n",
        "source-line insertion uses a zero-based insertion boundary");
    const auto removed = necwb::nec::removeSourceLine(source, 1);
    expect(removed && *removed == "GW 1 3 0 0 0 1 0 0 .001\nEN\n",
        "source-line deletion preserves remaining lines");
    expect(!necwb::nec::replaceSourceLine(source, 0, "invalid")
            && !necwb::nec::removeSourceLine(source, 99)
            && !necwb::nec::insertSourceLine(source, 99, "invalid"),
        "source-line editing rejects invalid positions");
}

void testSourceDocumentCache()
{
    necwb::nec::NecSourceCache cache;
    const std::string source =
        "SY length=2\nGW 1 3 0 0 0 length 0 0 .001\nGE 0\nEN\n";
    cache.update(source);
    const auto initialRevision = cache.revision();
    expect(cache.document().cards().size() == 4,
        "source cache parses the authored document");
    expect(cache.resolution().ok() && cache.resolvedDocument() != nullptr
            && cache.resolvedDocument()->cards()[1].fields[5] == "2",
        "source cache resolves and parses symbolic source");

    cache.update(source);
    expect(cache.revision() == initialRevision,
        "source cache reuses an unchanged source revision");

    cache.update("SY length=3\nGW 1 3 0 0 0 length 0 0 .001\nGE 0\nEN\n");
    expect(cache.revision() == initialRevision + 1
            && cache.resolvedDocument()->cards()[1].fields[5] == "3",
        "source cache rebuilds after a source edit");

    cache.update("SY length=missing\nGW 1 3 0 0 0 length 0 0 .001\nGE 0\nEN\n");
    expect(!cache.resolution().ok() && cache.resolvedDocument() == nullptr,
        "source cache does not retain a stale resolved document after an error");

    const auto invalidRevision = cache.revision();
    const auto invalidSource = std::string(cache.source());
    cache.invalidate();
    cache.update(invalidSource);
    expect(cache.revision() == invalidRevision + 1,
        "source cache rebuilds an explicitly invalidated revision");
}

void testKnownCardsAreRecognized()
{
    const std::string source = "CM note\nCE\nSY length=1\nGW 1 3 0 0 0 1 0 0 .001\nGS 0 0 1\nGE\nEX\nLD\nGN\nFR\nRP\nTL\nNT\nZ0 50\nZO 75\nEN";
    const auto document = necwb::nec::NecParser{}.parse(source);
    const auto cards = document.cards();
    expect(cards.size() == 16, "all known card lines are parsed");
    expect(cards[0].kind == necwb::nec::NecCardKind::Comment, "CM recognized");
    expect(cards[2].kind == necwb::nec::NecCardKind::Symbol, "SY recognized");
    expect(cards[3].kind == necwb::nec::NecCardKind::GeometryWire, "GW recognized");
    expect(cards[4].kind == necwb::nec::NecCardKind::GeometryScale, "GS recognized");
    expect(cards[13].kind == necwb::nec::NecCardKind::ReferenceImpedance
            && cards[14].kind == necwb::nec::NecCardKind::ReferenceImpedance,
        "canonical Z0 and legacy ZO reference impedance cards are recognized");
    expect(cards[15].kind == necwb::nec::NecCardKind::End, "EN recognized");
}

void testCompleteNec2CardCatalog()
{
    const auto catalog = necwb::nec::necCardCatalog();
    expect(catalog.size() == 37, "catalog contains the complete NEC-2 vocabulary and Workbench extensions");
    std::string source;
    for (const auto& spec : catalog) source += std::string(spec.mnemonic) + '\n';
    source += "ZZ 1\n";
    const auto document = necwb::nec::NecParser{}.parse(source);
    expect(document.cards().size() == catalog.size() + 1,
        "catalog deck retains every standard and extension card");
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        expect(document.cards()[index].kind != necwb::nec::NecCardKind::Unknown,
            "every catalog card is recognized");
    }
    expect(document.cards().back().kind == necwb::nec::NecCardKind::Unknown,
        "uncatalogued extensions remain distinguishable and preserved");
    expect(necwb::nec::NecWriter{}.write(document) == source,
        "recognized card coverage does not alter source round trips");
    expect(necwb::nec::findNecCardSpec("GA")->kind == necwb::nec::NecCardKind::GeometryOther
            && necwb::nec::findNecCardSpec("NE")->kind == necwb::nec::NecCardKind::ControlOther,
        "generated geometry and additional controls have stable semantic groups");

    constexpr std::array structuredCards{
        "GW", "GA", "GH", "SP", "SM", "SC", "GS", "GE", "FR", "GN", "LD", "EX",
        "TL", "RP", "XQ", "SY", "Z0", "ZO"};
    for (const auto mnemonic : structuredCards) {
        const auto* spec = necwb::nec::findNecCardSpec(mnemonic);
        expect(spec != nullptr
                && spec->support == necwb::nec::NecCardSupport::StructuredEditable,
            "dedicated structured card editors match the catalog support contract");
    }

    constexpr std::array understoodCards{
        "CM", "CE", "GC", "GM", "GX", "GR", "GF", "EK", "KH", "NT", "CP", "EN",
        "GD", "NE", "NH", "NX", "PQ", "PT", "WG"};
    for (const auto mnemonic : understoodCards) {
        const auto* spec = necwb::nec::findNecCardSpec(mnemonic);
        expect(spec != nullptr && spec->support == necwb::nec::NecCardSupport::Understood,
            "generic fixed-field cards remain distinguished from dedicated editors");
    }
}

void testSymbolResolution()
{
    const std::string source =
        "CM parameterized dipole\r\nCE\r\n"
        "SY half = 5, Height=2+4\r\n"
        "SY length=half*2, segments=2^4+5\r\n"
        "\r\n"
        "GW 1 segments -half 0 height half 0 Height 0.001\r\n"
        "GE 0\r\n"
        "EX 0 1 (segments+1)/2 0 1 0\r\n"
        "FR 0 1 0 0 7*2.025 0\r\n"
        "EN\r\n";
    const auto resolution = necwb::nec::NecSymbolResolver{}.resolve(source);
    expect(resolution.ok(), "valid symbols and expressions resolve without diagnostics");
    expect(resolution.definitions.size() == 4, "all symbol assignments are retained");
    expect(resolution.definitions[0].name == "half" && resolution.definitions[0].value == 5.0,
        "symbol definitions retain names and evaluated values");
    expect(resolution.definitions[0].expression == "5",
        "plain numeric assignment expressions are retained");
    expect(resolution.definitions[3].name == "segments" && resolution.definitions[3].value == 21.0,
        "later assignments can use arithmetic and earlier symbols");
    const auto scientificLiteral = necwb::nec::NecSymbolResolver{}.resolve(
        "SY radius=1.0e-3, offset=+2.5\n");
    expect(scientificLiteral.ok() && scientificLiteral.definitions.size() == 2,
        "signed and scientific numeric assignments remain available");
    const auto derivedOverride = necwb::nec::NecSymbolResolver{}.resolve(
        "SY FT=0.3048\nSY len=66*FT\nGW 1 11 0 0 0 len 0 0 0.001\nGE 0\n",
        {{"len", 10.0}});
    expect(derivedOverride.ok()
            && derivedOverride.generatedDeck.find("GW 1 11 0 0 0 10 0 0 0.001")
                != std::string::npos,
        "expression-based symbols accept candidate overrides before downstream use");
    expect(resolution.generatedDeck.find("SY ") == std::string::npos,
        "generated numeric deck omits Workbench symbol declarations");

    const std::string editableSymbols =
        "CM symbols\r\nSY length=10, half=length/2\r\nGW 1 11 -half 0 0 half 0 0 .001\r\nEN\r\n";
    const auto insertedSymbol = necwb::nec::insertSymbolDefinition(
        editableSymbols, "height", "6*0.3048");
    expect(insertedSymbol.find("SY height=6*0.3048\r\nGW") != std::string::npos,
        "new symbol is inserted after existing SY definitions with line endings preserved");
    const auto replacedSymbol = necwb::nec::replaceSymbolDefinition(
        editableSymbols, 2, "half", "halfLength", "length/2+0.1");
    expect(replacedSymbol && replacedSymbol->find(
        "SY length=10, halfLength=length/2+0.1") != std::string::npos,
        "one assignment on a multi-symbol SY line can be replaced");
    const auto removedSymbol = necwb::nec::removeSymbolDefinition(
        editableSymbols, 2, "length");
    expect(removedSymbol && removedSymbol->find("SY half=length/2") != std::string::npos
            && removedSymbol->find("SY length=") == std::string::npos,
        "one assignment on a multi-symbol SY line can be removed");
    const std::string fixedCard =
        "CM fixed geometry\r\nGW 1 11 -5 0 6 5 0 6 0.001\r\nGE 0\r\n";
    const auto parameterized = necwb::nec::parameterizeNecCardField(
        fixedCard, 2, 2, "wire_x1");
    expect(parameterized
            && parameterized->find("SY wire_x1=5\r\nGW 1 11 -wire_x1 0 6 5 0 6 0.001")
                != std::string::npos,
        "a negative numeric field becomes a positive SY magnitude with a negated reference");
    const auto parameterizedResolution = parameterized
        ? necwb::nec::NecSymbolResolver{}.resolve(*parameterized)
        : necwb::nec::SymbolResolution{};
    expect(parameterizedResolution.ok()
            && parameterizedResolution.generatedDeck.find(
                "GW 1 11 -5 0 6 5 0 6 0.001") != std::string::npos,
        "a promoted field resolves back to the original numeric NEC card");
    const auto symmetricReference = necwb::nec::symbolReferencePreservingValue(
        "wire_x1", 5.0, 5.0);
    const auto symmetricWire = parameterized
        ? necwb::nec::replaceNecCardFieldExpression(
            *parameterized, 3, 5, symmetricReference)
        : std::optional<std::string>{};
    expect(symmetricWire && symmetricWire->find(
            "SY wire_x1=5\r\nGW 1 11 -wire_x1 0 6 wire_x1 0 6 0.001")
                != std::string::npos,
        "one positive SY magnitude controls opposite signed symmetric wire endpoints");
    const auto detachedSymmetricField = symmetricWire
        ? necwb::nec::replaceNecCardFieldExpression(*symmetricWire, 3, 2, "-5")
        : std::optional<std::string>{};
    expect(detachedSymmetricField && detachedSymmetricField->find(
            "GW 1 11 -5 0 6 wire_x1 0 6 0.001") != std::string::npos,
        "detaching a signed parameter writes the resolved number into the raw GW field");
    expect(necwb::nec::symbolReferencePreservingValue("half_length", 5.0, -5.0)
                == "-half_length"
            && necwb::nec::symbolReferencePreservingValue("half_length", 5.0, 5.0)
                == "half_length"
            && necwb::nec::symbolReferencePreservingValue("offset", 3.0, 4.0)
                == "offset",
        "existing parameters preserve matching direct and opposite field signs");
    expect(!necwb::nec::parameterizeNecCardField(
            "FR 0 1 0 0 start 0\n", 1, 4, "frequency"),
        "an existing symbolic field is not promoted a second time");
    const auto relinkedField = necwb::nec::replaceNecCardFieldExpression(
        "SY first=5\nSY second=6\nGW 1 11 first 0 0 10 0 0 .001\n",
        3, 2, "second");
    expect(relinkedField && relinkedField->find(
            "GW 1 11 second 0 0 10 0 0 .001") != std::string::npos,
        "a parameter-controlled field can be linked to a different existing symbol");
    const auto detachedField = relinkedField
        ? necwb::nec::replaceNecCardFieldExpression(*relinkedField, 3, 2, "6")
        : std::optional<std::string>{};
    expect(detachedField && detachedField->find(
            "GW 1 11 6 0 0 10 0 0 .001") != std::string::npos,
        "detaching a parameter replaces the raw NEC card field with its resolved numeric value");
    const auto controlledFields = necwb::nec::necCardFieldsReferencingSymbols(
        "SY length=10, half=length/2\nGW 1 11 -half 0 0 half 0 0 0.001\n"
        "FR 0 1 0 0 14+0.175 0\n", std::array<std::string, 2>{"length", "half"});
    expect(controlledFields.contains(2)
            && controlledFields.at(2).at(2) == "-half"
            && controlledFields.at(2).at(5) == "half"
            && !controlledFields.contains(3),
        "parameter-controlled field detection distinguishes SY references from numeric expressions");
    expect(resolution.resolvedSource.find("CE\r\n\r\n\r\n\r\nGW") != std::string::npos,
        "line-preserving resolution replaces each SY declaration with a blank source line");
    expect(resolution.generatedDeck.find("GW 1 21 -5 0 6 5 0 6 0.001") != std::string::npos,
        "symbolic geometry fields become numeric NEC fields");
    expect(resolution.generatedDeck.find("EX 0 1 11 0 1 0") != std::string::npos,
        "integer expressions resolve for segment references");
    expect(resolution.generatedDeck.find("FR 0 1 0 0 14.175 0") != std::string::npos,
        "arithmetic-only fields resolve without a named symbol");
    expect(resolution.generatedDeck.find("\r\n\r\nGW") != std::string::npos,
        "retained blank lines and CRLF endings survive numeric generation");
    expect(!resolution.generatedDeck.empty() && resolution.generatedDeck.ends_with("\r\n"),
        "generated deck preserves the final line ending");
    const auto resolvedDocument = necwb::nec::NecParser{}.parse(resolution.resolvedSource);
    const auto checked = necwb::nec::NecModelChecker{}.check(resolvedDocument);
    expect(checked.errorCount() == 0 && checked.model.wireCount() == 1,
        "resolved deck is valid input for the existing NEC model pipeline");
    expect(checked.model.wires().front().sourceLine == 6,
        "line-preserving resolution keeps semantic objects mapped to parameterized source lines");
    const auto generatedDocument = necwb::nec::NecParser{}.parse(resolution.generatedDeck);
    const auto generatedSetup = necwb::nec::NecSetupConverter{}.convert(generatedDocument);
    expect(generatedSetup.frequency && generatedSetup.frequency->startMHz == 14.175,
        "generated numeric deck converts through the existing setup pipeline");
    const auto solverInput = necwb::analysis::prepareSolverInput(
        resolution.generatedDeck, generatedSetup);
    expect(solverInput.find("SY ") == std::string::npos
            && solverInput.find("FR 0 1 0 0 14.175 0") != std::string::npos,
        "native solver input contains resolved numeric cards and no Workbench symbols");
    const std::string tabbedFourNec2Input =
        "CM G5RV Multiband Antenna\r\n"
        "CE\r\n"
        "GW\t1\t31\t0\t-15\t0\t0\t15\t0\t.003\r\n"
        "GE\t0\r\n"
        "GN\t-1\r\n"
        "EK\r\n"
        "EX\t0\t1\t16\t0\t1.0\t0\t0\t0\r\n"
        "FR\t0\t0\t0\t0\t14.2\t0\r\n"
        "XQ\r\n"
        "EN\r\n";
    const auto fourNec2Input = necwb::analysis::formatSolverInputForBackend(
        tabbedFourNec2Input, "nec2dxs");
    expect(fourNec2Input.find('\t') == std::string::npos
            && fourNec2Input.find("GW 1 31 0 -15 0 0 15 0 .003\r\n")
                != std::string::npos
            && fourNec2Input.find("EX 0 1 16 0 1.0 0 0 0\r\n")
                != std::string::npos
            && fourNec2Input.ends_with("EN\r\n"),
        "4nec2 NEC2dXS input uses canonical space-delimited fields and preserves line endings");
    expect(necwb::analysis::formatSolverInputForBackend(
               tabbedFourNec2Input, "nec2") == tabbedFourNec2Input
            && necwb::analysis::formatSolverInputForBackend(
                tabbedFourNec2Input, "opennec") == tabbedFourNec2Input,
        "backend input formatting leaves NEC2C and OpenNEC decks unchanged");
    const auto overridden = necwb::nec::NecSymbolResolver{}.resolve(source, {{"HALF", 7.5}});
    expect(overridden.ok()
            && overridden.generatedDeck.find("GW 1 21 -7.5 0 6 7.5 0 6 0.001") != std::string::npos,
        "case-insensitive symbol overrides generate optimization candidates");
    const auto impedanceInput = necwb::analysis::prepareImpedanceInput(
        "FR 0 1 0 0 7.1 0\nRP 0 37 36 1000 0 0 5 10\nEN\n");
    expect(impedanceInput.find("RP ") == std::string::npos,
        "impedance-only solver input removes expensive radiation requests");
    expect(impedanceInput.find("XQ 0\nEN") != std::string::npos,
        "impedance-only solver input inserts an execution request before EN");
    const auto existingExecution = necwb::analysis::prepareImpedanceInput(
        "FR 0 1 0 0 7.1 0\r\nXQ 0\r\nEN\r\n");
    expect(existingExecution == "FR 0 1 0 0 7.1 0\r\nXQ 0\r\nEN\r\n",
        "impedance-only input preserves an existing execution request and line endings");
    const std::array explicitFrequencies{21.2, 7.15, 7.0, 7.15,
        std::numeric_limits<double>::quiet_NaN(), -1.0};
    const auto explicitInput = necwb::analysis::prepareExplicitFrequencyInput(
        "FR 0 16 0 0 7 0.02\r\nRP 0 181 1 1000 0 0 1 0 0 0\r\nXQ 0\r\nEN\r\n",
        explicitFrequencies);
    expect(explicitInput ==
            "FR 0 1 0 0 7 0\r\nXQ 0\r\n"
            "FR 0 1 0 0 7.15 0\r\nXQ 0\r\n"
            "FR 0 1 0 0 21.2 0\r\nXQ 0\r\nEN\r\n",
        "explicit-frequency input sorts and deduplicates valid points while replacing old requests");
    const necwb::model::FrequencyDefinition quickSweep{0, 36, 14.0, 0.01, 0};
    const auto quickImpedance = necwb::analysis::prepareFrequencySweepInput(
        "GW 1 11 0 0 0 1 0 0 .001\r\nGE 0\r\nEX 0 1 6 0 1 0\r\n"
        "FR 0 1 0 0 7.1 0\r\nRP 0 19 37 1000 0 0 5 10\r\nEN\r\n",
        quickSweep, false);
    const auto quickImpedanceDocument = necwb::nec::NecParser{}.parse(quickImpedance);
    const auto quickImpedanceSetup = necwb::nec::NecSetupConverter{}.convert(
        quickImpedanceDocument);
    expect(quickImpedanceSetup.frequency && quickImpedanceSetup.frequency->count == 36
            && std::abs(quickImpedanceSetup.frequency->startMHz - 14.0) < 1.0e-12
            && std::abs(quickImpedanceSetup.frequency->step - 0.01) < 1.0e-12
            && quickImpedanceSetup.radiationPatterns.empty()
            && quickImpedanceSetup.executionRequest.has_value()
            && quickImpedance.find("\r\n") != std::string::npos,
        "quick impedance sweep replaces FR, removes RP, inserts XQ, and preserves line endings");
    const auto quickRadiation = necwb::analysis::prepareFrequencySweepInput(
        "FR 0 1 0 0 7.1 0\nXQ 0\nRP 0 19 37 1000 0 0 5 10\nEN\n",
        quickSweep, true);
    const auto quickRadiationDocument = necwb::nec::NecParser{}.parse(quickRadiation);
    const auto quickRadiationSetup = necwb::nec::NecSetupConverter{}.convert(
        quickRadiationDocument);
    expect(quickRadiationSetup.frequency && quickRadiationSetup.frequency->count == 36
            && quickRadiationSetup.radiationPatterns.size() == 1
            && !quickRadiationSetup.executionRequest.has_value()
            && quickRadiation.find("FR 0 36 0 0 14 0.01\nRP") != std::string::npos,
        "quick radiation sweep retains RP while replacing authored FR and XQ requests");

    const auto invalid = necwb::nec::NecSymbolResolver{}.resolve(
        "SY first=missing+1, good=2, GOOD=3, zero=1/0\n"
        "GW 1 unknown 0 0 0 1 0 0 .001\n"
        "XX textual-extension\n");
    expect(!invalid.ok() && invalid.diagnostics.size() == 4,
        "unknown, duplicate, zero-divisor, and unresolved field errors are reported");
    expect(invalid.definitions.size() == 1 && invalid.definitions.front().name == "good",
        "invalid assignments do not hide independent valid definitions");
    expect(invalid.generatedDeck.find("XX textual-extension") != std::string::npos,
        "unsupported card text remains untouched during symbol resolution");
}

void testCardFieldEditingPreservesExpressions()
{
    const std::string wire = "GW  1  11  0  -hlen  height  0  hlen  height  rad";
    const std::array wireReplacement{
        necwb::nec::NecFieldReplacement{1, "29"}};
    const auto updatedWire = necwb::nec::replaceNecCardFields(wire, wireReplacement);
    expect(updatedWire && *updatedWire
            == "GW  1  29  0  -hlen  height  0  hlen  height  rad",
        "field editing changes only the selected GW field");
    expect(necwb::nec::necCardFieldIsNumeric(wire, 0)
            && !necwb::nec::necCardFieldIsNumeric(wire, 3)
            && !necwb::nec::necCardFieldIsNumeric(wire, 8),
        "symbolic GW geometry fields are distinguished from numeric fields");
    expect(necwb::nec::necCardFieldIsNumeric("GW 1 11 +1D-3 0 0 1 0 0 .001", 2),
        "signed Fortran-exponent fields remain numeric");

    const std::string line = "TL 1 11 2 9 z0 electrical_len 0 0 0 0";
    const std::array lineReplacements{
        necwb::nec::NecFieldReplacement{1, "15"},
        necwb::nec::NecFieldReplacement{3, "13"}};
    const auto updatedLine = necwb::nec::replaceNecCardFields(line, lineReplacements);
    expect(updatedLine && *updatedLine
            == "TL 1 15 2 13 z0 electrical_len 0 0 0 0",
        "field editing preserves unrelated symbolic TL values");
}

void testWireConversion()
{
    const std::string source = "GW 7 21 -5 0 10 5 0 10 .001\nEN\n";
    const auto document = necwb::nec::NecParser{}.parse(source);
    const auto result = necwb::nec::NecModelConverter{}.convert(document);
    expect(result.issues.empty(), "valid GW has no conversion issues");
    expect(result.model.wireCount() == 1, "valid GW creates one wire");
    const auto& wire = result.model.wires().front();
    expect(wire.tag == 7, "wire tag is converted");
    expect(wire.segments == 21, "wire segments are converted");
    expect(wire.start.x == -5.0 && wire.end.x == 5.0, "wire endpoints are converted");
    expect(wire.radius == 0.001, "wire radius is converted");
    expect(wire.sourceLine == 1, "wire retains source mapping");
}

void testInvalidWireIsDiagnosed()
{
    const std::string source = "GW 1 0 0 0 0 0 0 0 -1\nGW malformed\n";
    const auto result = necwb::nec::NecModelConverter{}.convert(necwb::nec::NecParser{}.parse(source));
    expect(result.model.empty(), "invalid wires do not enter semantic model");
    expect(result.issues.size() == 2, "invalid wires produce diagnostics");
    expect(result.issues[0].lineNumber == 1 && result.issues[1].lineNumber == 2,
        "diagnostics retain source lines");
}

void testGeometryScaleConversion()
{
    const auto document = necwb::nec::NecParser{}.parse(
        "GW 1 3 0 0 0 10 0 0 .1\n"
        "GS 0 0 0.3048\n"
        "GW 2 3 0 0 0 10 0 0 .1\n"
        "GE 0\n");
    const auto result = necwb::nec::NecModelConverter{}.convert(document);
    expect(result.issues.empty() && result.model.wireCount() == 2,
        "valid GS geometry converts without issues");
    expect(std::abs(result.model.wires()[0].end.x - 3.048) < 1.0e-12
            && std::abs(result.model.wires()[0].radius - 0.03048) < 1.0e-12,
        "GS scales preceding wire coordinates and radius to meters");
    expect(result.model.wires()[1].end.x == 10.0 && result.model.wires()[1].radius == 0.1,
        "GS does not scale geometry cards that follow it");

    const auto feetDocument = necwb::nec::NecParser{}.parse(
        "GW 1 3 0 0 0 10 0 0 .1\nGS 0 0 0.3048\nGE 0\n");
    const auto units = necwb::nec::inspectDeckGeometryUnits(feetDocument);
    expect(units.uniform && units.standardUnit == necwb::model::LengthUnit::Foot
            && units.scaleToMeters == 0.3048,
        "a trailing 0.3048 GS card identifies feet deck geometry");

    const necwb::model::Wire physical{3, {0.0, 0.0, 0.0}, {3.048, 0.0, 0.0}, 5,
        0.03048, 1};
    const auto feetSource = necwb::nec::NecWriter{}.writeWireCard(physical, 0.3048)
        + "\nGS 0 0 0.3048\nGE 0\n";
    const auto roundTrip = necwb::nec::NecModelConverter{}.convert(
        necwb::nec::NecParser{}.parse(feetSource));
    expect(roundTrip.issues.empty()
            && std::abs(roundTrip.model.wires().front().end.x - physical.end.x) < 1.0e-12
            && std::abs(roundTrip.model.wires().front().radius - physical.radius) < 1.0e-12,
        "deck-unit wire writing and GS conversion preserve physical geometry");

    const auto invalid = necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
        "GW 1 3 0 0 0 1 0 0 .001\nGS 0 0 -1\nGE 0\n"));
    expect(std::ranges::any_of(invalid.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("GS requires") != std::string::npos;
    }), "invalid GS scale factors are diagnosed");
}

void testGeneratedWireGeometryConversion()
{
    const auto document = necwb::nec::NecParser{}.parse(
        "GW 1 4 0 0 0 4 0 0 0\n"
        "GC 0 0 2 0.01 0.02\n"
        "GA 2 4 1 0 90 0.01\n"
        "GH 3 8 0.5 1 0.25 0.25 0.25 0.25 0.01\n"
        "GS 0 0 2\n"
        "GE 0\n");
    const auto result = necwb::nec::NecModelConverter{}.convert(document);
    expect(result.issues.empty() && result.model.wireCount() == 3,
        "GC, GA, and GH produce semantic wire geometry");

    const auto* tapered = result.model.wireByTag(1);
    const auto* arc = result.model.wireByTag(2);
    const auto* helix = result.model.wireByTag(3);
    expect(tapered != nullptr && !tapered->editable && tapered->path.size() == 5
            && std::abs(tapered->radius - 0.02) < 1.0e-12
            && std::abs(tapered->endRadius - 0.04) < 1.0e-12
            && std::abs(tapered->path[1].x - 8.0 / 15.0) < 1.0e-12,
        "GC preserves tapered segment boundaries and endpoint radii through GS scaling");
    expect(arc != nullptr && arc->geometryKind == necwb::model::WireGeometryKind::Arc
            && arc->path.size() == 5 && std::abs(arc->start.x - 2.0) < 1.0e-12
            && std::abs(arc->end.z - 2.0) < 1.0e-12,
        "GA expands into scaled chord points in the XZ plane");
    expect(helix != nullptr && helix->geometryKind == necwb::model::WireGeometryKind::Helix
            && helix->path.size() == 9 && std::abs(helix->start.x - 0.5) < 1.0e-12
            && std::abs(helix->end.z - 2.0) < 1.0e-12,
        "GH expands into a scaled helix polyline");
    const auto helixSegment = necwb::model::wireSegmentCenter(*helix, 2);
    expect(helixSegment && std::abs(helixSegment->z - 0.375) < 1.0e-12,
        "attachments resolve against generated curved-wire segment positions");

    const auto invalid = necwb::nec::NecModelConverter{}.convert(necwb::nec::NecParser{}.parse(
        "GW 1 3 0 0 0 1 0 0 0\nGE 0\n"));
    expect(invalid.model.empty() && !invalid.issues.empty(),
        "a zero-radius GW without a following GC card is rejected");

    const auto spiral = necwb::nec::NecModelConverter{}.convert(necwb::nec::NecParser{}.parse(
        "GH 8 40 0.05 0 0.1 0.1 0.2 0.2 0.001\nGE 0\n"));
    expect(spiral.model.empty() && spiral.issues.empty()
            && spiral.model.unrenderedSegments().size() == 1
            && spiral.model.unrenderedSegments()[0].tag == 8
            && spiral.model.unrenderedSegments()[0].segments == 40,
        "valid flat-spiral GH remains preserved without claiming graphical expansion");
}

void testGeometryTransformConversion()
{
    const auto moved = necwb::nec::NecModelConverter{}.convert(necwb::nec::NecParser{}.parse(
        "GW 5 1 0 2 0 0 2 1 .001\n"
        "GW 1 1 1 0 0 2 0 0 .001\n"
        "GM 10 2 0 0 90 0 1 0 1\n"
        "GE 0\n"));
    expect(moved.issues.empty() && moved.model.wireCount() == 4,
        "GM retains earlier tags and adds the requested successive copies");
    expect(moved.model.wires()[0].tag == 5
            && moved.model.wires()[1].tag == 1
            && moved.model.wires()[2].tag == 11
            && moved.model.wires()[3].tag == 21,
        "GM starts at its selected tag and increments generated tags successively");
    expect(std::abs(moved.model.wires()[2].start.x) < 1.0e-12
            && std::abs(moved.model.wires()[2].start.y - 2.0) < 1.0e-12
            && std::abs(moved.model.wires()[3].start.x + 2.0) < 1.0e-12
            && std::abs(moved.model.wires()[3].start.y - 1.0) < 1.0e-12,
        "GM applies rotation then translation to each preceding generated copy");
    expect(!moved.model.wires()[2].editable
            && moved.model.wires()[2].sourceLine == 3,
        "GM-generated geometry remains read-only and maps to the transform card");

    const auto reflected = necwb::nec::NecModelConverter{}.convert(
        necwb::nec::NecParser{}.parse(
            "GW 1 1 1 1 0 2 1 0 .001\nGX 10 110\nGE 0\n"));
    expect(reflected.issues.empty() && reflected.model.wireCount() == 4,
        "GX expands two selected reflection planes");
    expect(reflected.model.wires()[0].tag == 1
            && reflected.model.wires()[1].tag == 11
            && reflected.model.wires()[2].tag == 21
            && reflected.model.wires()[3].tag == 31,
        "GX doubles the tag increment after each Z-Y-X reflection stage");
    expect(reflected.model.wires()[1].start == necwb::model::Point3D{1.0, -1.0, 0.0}
            && reflected.model.wires()[2].start == necwb::model::Point3D{-1.0, 1.0, 0.0}
            && reflected.model.wires()[3].start == necwb::model::Point3D{-1.0, -1.0, 0.0},
        "GX produces every requested coordinate-plane image");

    const auto rotated = necwb::nec::NecModelConverter{}.convert(
        necwb::nec::NecParser{}.parse(
            "GA 1 2 .5 0 90 .001\nGR 10 4\nGE 0\n"));
    expect(rotated.issues.empty() && rotated.model.wireCount() == 4,
        "GR total copy count includes the original structure");
    expect(rotated.model.wires()[1].tag == 11
            && rotated.model.wires()[2].tag == 21
            && rotated.model.wires()[3].tag == 31
            && rotated.model.wires()[1].path.size() == 3,
        "GR increments tags and preserves generated curved paths");
    expect(std::abs(rotated.model.wires()[1].start.x) < 1.0e-12
            && std::abs(rotated.model.wires()[1].start.y - 0.5) < 1.0e-12,
        "GR rotates generated geometry uniformly around the Z axis");

    const auto invalidReflection = necwb::nec::NecModelConverter{}.convert(
        necwb::nec::NecParser{}.parse(
            "GW 1 1 -1 0 0 1 0 0 .001\nGX 10 100\nGE 0\n"));
    expect(invalidReflection.model.wireCount() == 1
            && invalidReflection.issues.size() == 1,
        "GX diagnoses geometry that crosses a selected symmetry plane");
}

void testSurfacePatchConversion()
{
    const auto result = necwb::nec::NecModelConverter{}.convert(
        necwb::nec::NecParser{}.parse(
            "SP 0 0 0 0 0 90 0 4\n"
            "SP 0 1 0 0 1 2 0 1\n"
            "SC 0 0 2 1 1\n"
            "SP 0 2 0 0 2 2 0 2\n"
            "SC 0 0 1 1 2\n"
            "SP 0 3 0 0 3 2 0 3\n"
            "SC 0 0 2 1 3 0 1 3\n"
            "SM 2 1 0 0 4 2 0 4\n"
            "SC 0 0 2 1 4\n"
            "GS 0 0 2\n"
            "GE 0\n"));
    expect(result.issues.empty() && result.model.surfacePatchCount() == 6,
        "SP/SC shapes and SM grids produce separate semantic surface patches");
    expect(result.model.wireCount() == 0 && !result.model.empty(),
        "a patch-only deck is valid semantic geometry without fake wires");
    const auto patches = result.model.surfacePatches();
    expect(patches[0].kind == necwb::model::SurfacePatchKind::Arbitrary
            && patches[0].corners.size() == 4
            && std::abs(patches[0].corners[0].x + 2.0) < 1.0e-12,
        "arbitrary SP center, normal, and area expand into a renderable square");
    expect(patches[1].kind == necwb::model::SurfacePatchKind::Rectangular
            && patches[1].corners.size() == 4
            && patches[1].corners[3] == necwb::model::Point3D{0.0, 2.0, 2.0},
        "rectangular SP infers its fourth corner and follows GS scaling");
    expect(patches[2].kind == necwb::model::SurfacePatchKind::Triangular
            && patches[2].corners.size() == 3,
        "triangular SP preserves three explicit corners");
    expect(patches[3].kind == necwb::model::SurfacePatchKind::Quadrilateral
            && patches[3].corners.size() == 4,
        "quadrilateral SP preserves all four explicit corners");
    expect(patches[4].kind == necwb::model::SurfacePatchKind::GridCell
            && patches[5].kind == necwb::model::SurfacePatchKind::GridCell
            && patches[4].sourceLine == 8 && patches[5].sourceLine == 8,
        "SM subdivides its surface and retains the parent source line");

    const auto transformed = necwb::nec::NecModelConverter{}.convert(
        necwb::nec::NecParser{}.parse(
            "SP 0 1 1 0 0 2 0 0\nSC 0 0 2 1 0\nGR 0 4\nGE 0\n"));
    expect(transformed.issues.empty() && transformed.model.surfacePatchCount() == 4,
        "GR duplicates surface patches around the Z axis");
    expect(std::abs(transformed.model.surfacePatches()[1].corners[0].x) < 1.0e-12
            && std::abs(transformed.model.surfacePatches()[1].corners[0].y - 1.0) < 1.0e-12,
        "surface patch transformations use the same coordinates as wire transformations");

    const auto invalid = necwb::nec::NecModelConverter{}.convert(
        necwb::nec::NecParser{}.parse("SP 0 3 0 0 0 1 0 0\nGE 0\nSC 0 0 1 1 0 0 1 0\n"));
    expect(invalid.model.empty() && invalid.issues.size() == 2,
        "missing and orphaned SC patch continuations are diagnosed");
}

void testValidModelCheck()
{
    const std::string source =
        "GW 1 21 -5 0 10 5 0 10 .001\n"
        "GE 0\n"
        "EX 0 1 11 0 1 0\n"
        "FR 0 1 0 0 14.175 0\n"
        "EN\n";
    const auto document = necwb::nec::NecParser{}.parse(source);
    const auto result = necwb::nec::NecModelChecker{}.check(document);
    expect(result.errorCount() == 0, "valid model has no errors");
    expect(result.warningCount() == 0, "valid model has no warnings");
    expect(result.model.wireCount() == 1, "model check returns semantic geometry");

    const std::string omittedIncrementSource =
        "GW 1 21 -5 0 10 5 0 10 .001\n"
        "GE 0\n"
        "EX 0 1 11 0 1 0\n"
        "FR 0 1 0 0 14.175\n"
        "EN\n";
    const auto omittedIncrementResolution =
        necwb::nec::NecSymbolResolver{}.resolve(omittedIncrementSource);
    const auto omittedIncrementDocument = necwb::nec::NecParser{}.parse(
        omittedIncrementResolution.resolvedSource);
    const auto omittedIncrementResult =
        necwb::nec::NecModelChecker{}.check(omittedIncrementDocument);
    const auto omittedIncrementSetup =
        necwb::nec::NecSetupConverter{}.convert(omittedIncrementDocument);
    expect(omittedIncrementResult.diagnostics.empty(),
        "single-frequency FR silently accepts an omitted increment");
    expect(omittedIncrementSetup.frequency
            && omittedIncrementSetup.frequency->startMHz == 14.175
            && omittedIncrementSetup.frequency->step == 0.0,
        "omitted single-frequency increment converts to zero");
    expect(omittedIncrementResolution.resolvedSource == omittedIncrementSource,
        "symbol resolution preserves the authored FR line without an increment");
    expect(omittedIncrementResolution.generatedDeck.find("FR 0 1 0 0 14.175 0")
            != std::string::npos,
        "generated solver deck supplies zero for an omitted single-frequency increment");

    const auto commaResolution = necwb::nec::NecSymbolResolver{}.resolve(
        "GW 1,21,0,-10.64,0,0,+10.64,0,.004\nGE 0\n"
        "EX 0,1,11,0,1\nFR 0,0,0,0,7.05\nEN\n");
    expect(commaResolution.ok()
            && necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
                   commaResolution.resolvedSource)).errorCount() == 0,
        "symbol preprocessing preserves valid comma-delimited NEC fields");

    const auto missingSweepIncrement = necwb::nec::NecModelChecker{}.check(
        necwb::nec::NecParser{}.parse(
            "GW 1 21 -5 0 10 5 0 10 .001\nGE 0\nEX 0 1 11 0 1 0\n"
            "FR 0 2 0 0 14.175\nEN\n"));
    expect(missingSweepIncrement.errorCount() == 1,
        "multi-frequency FR still requires an increment");
}

void testStaticModelAdequacyChecks()
{
    const std::string source =
        "GW 1 2 0 0 0 10 0 0 1\n"
        "GE 0\n"
        "EX 0 1 1 0 1 0\n"
        "FR 0 1 0 0 300 0\n"
        "EN\n";
    const auto result = necwb::nec::NecModelChecker{}.check(
        necwb::nec::NecParser{}.parse(source));
    expect(result.errorCount() == 0, "adequacy findings do not block an otherwise valid model");
    expect(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.category == "Model adequacy"
            && diagnostic.message.find("0.1 wavelength") != std::string::npos;
    }), "model adequacy warns about electrically long segments");
    expect(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.category == "Model adequacy"
            && diagnostic.message.find("segment-length/diameter") != std::string::npos;
    }), "model adequacy warns about thick-wire segment ratios");
    expect(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.category == "Model adequacy"
            && diagnostic.message.find("even segment count") != std::string::npos;
    }), "model adequacy warns when a center feed has no center segment");
}

void testCardValidation()
{
    const std::string source =
        "GW 1 3 0 0 0 1 0 0 .001\n"
        "GE 0\n"
        "EX 0 1 bad 0 1 0\n"
        "FR 0 -1 0 0 -14 0\n"
        "ZZ unsupported\n"
        "EX 0 1 2 0 1 0\n"
        "FR 0 1 0 0 14 0\n"
        "EN\n";
    const auto result = necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(source));
    expect(result.errorCount() == 3, "invalid EX and FR values produce errors");
    expect(result.warningCount() == 1, "unknown card produces a warning");
    expect(result.diagnostics.front().lineNumber == 3, "card diagnostic retains source line");
}

void testSharedTagReferences()
{
    // NEC reads EX/LD/TL tag references as the m-th segment among all wires with that tag,
    // and tag 0 as an absolute segment number.
    const std::string deck =
        "GW 1 11 0 -0.5 0 0 0.5 0 0.003\n"
        "GW 1 11 0.2 0.5 0 0.2 -0.5 0 0.003\n"
        "GW 0 5 0.4 -0.3 0 0.4 0.3 0 0.003\n"
        "GE 0\n";
    const auto check = [&deck](const std::string& cards) {
        return necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
            deck + cards + "FR 0 1 0 0 146 0\nEN\n"));
    };
    const auto shared = check("EX 0 1 17 0 1 0\nLD 0 1 12 22 50 0 0\nTL 1 6 0 25 50 0\n");
    expect(shared.errorCount() == 0,
        "segments past the first wire of a shared tag are valid references");
    expect(check("EX 0 1 23 0 1 0\n").errorCount() == 1
            && check("EX 0 0 28 0 1 0\n").errorCount() == 1
            && check("TL 1 23 1 1 50 0\n").errorCount() == 1,
        "references past every wire with the tag, or past the structure, are rejected");
    expect(check("EX 0 0 27 0 1 0\n").errorCount() == 0,
        "tag 0 refers to an absolute segment number");

    const auto& model = shared.model;
    const auto reference = necwb::model::referencedSegment(model, 1, 17);
    const auto absolute = necwb::model::referencedSegment(model, 0, 24);
    expect(reference && reference->wire == &model.wires()[1] && reference->segment == 6
            && absolute && absolute->wire == &model.wires()[2] && absolute->segment == 2
            && necwb::model::referencedSegmentCount(model, 1) == 22
            && necwb::model::referencedSegmentCount(model, 0) == 27
            && necwb::model::hasSharedWireTags(model),
        "tag references resolve to the wire NEC uses");
    const auto marker = necwb::model::wireSegmentPosition(model, 1, 17);
    expect(marker && std::abs(marker->x - 0.2) < 1.0e-12 && std::abs(marker->y) < 1.0e-12,
        "a source on the second wire of a shared tag is drawn on that wire");
    necwb::model::AntennaModel separateTags;
    separateTags.addWire({1, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 3, 0.001, 1});
    separateTags.addWire({2, {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}, 3, 0.001, 2});
    necwb::model::ModelSetup globalLoad;
    globalLoad.loads.push_back({.wireTag = 0});
    necwb::model::ModelSetup rangedGlobalLoad;
    rangedGlobalLoad.loads.push_back({.wireTag = 0, .firstSegment = 2, .lastSegment = 4});
    expect(!necwb::model::hasCrossWireSegmentReferences(separateTags, globalLoad)
            && necwb::model::hasCrossWireSegmentReferences(separateTags, rangedGlobalLoad)
            && necwb::model::hasCrossWireSegmentReferences(model, {}),
        "segment remapping is refused for shared tags and absolute tag-0 ranges");
    expect(necwb::analysis::prepareSegmentationConvergenceInput(
               deck + "EX 0 1 17 0 1 0\nFR 0 1 0 0 146 0\nEN\n", 146.0, 2.0).error
               .find("share a tag") != std::string::npos,
        "convergence testing refuses decks whose tag references it cannot remap");
}

void testUnrenderedSegmentNumbering()
{
    // NEC numbers a flat spiral's segments even though Workbench does not draw it, so the
    // GW after a 40-segment spiral holds segments 41-43.
    const std::string deck =
        "GH 8 40 0.05 0 0.1 0.1 0.2 0.2 0.001\n"
        "GW 1 3 0 -1 3 0 1 3 0.001\n"
        "GE 0\n";
    const auto check = [&deck](const std::string& cards) {
        return necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
            deck + cards + "FR 0 1 0 0 146 0\nEN\n"));
    };
    const auto spiralFirst = check("EX 0 0 42 0 1 0\n");
    expect(spiralFirst.errorCount() == 0
            && check("EX 0 8 40 0 1 0\n").errorCount() == 0
            && check("EX 0 0 44 0 1 0\n").errorCount() == 1,
        "absolute and spiral-tag references count the undrawn spiral's segments");
    expect(std::ranges::count_if(spiralFirst.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("Flat-spiral GH") != std::string::npos
            && diagnostic.lineNumber == 1;
    }) == 1, "a flat spiral is flagged as unreliable in many solvers");

    const auto& model = spiralFirst.model;
    const auto marker = necwb::model::wireSegmentPosition(model, 0, 42);
    expect(necwb::model::referencedSegmentCount(model, 0) == 43
            && marker && std::abs(marker->y) < 1.0e-12 && std::abs(marker->z - 3.0) < 1.0e-12
            && !necwb::model::wireSegmentPosition(model, 8, 5)
            && !necwb::model::hasSharedWireTags(model),
        "a tag-0 marker after a spiral lands on the drawn wire, and none is drawn on the spiral");
    const necwb::analysis::WireSegmentIndex index(model);
    const auto located = index.locate(1, 41);
    expect(located && located->wireIndex == 0 && located->segment == 1
            && !index.locate(1, 1) && !index.locate(8, 5),
        "the current index skips the spiral's segments before the first drawn wire");

    // GM and GX copy the spiral along with the wire, so the copies keep NEC's order.
    const auto copied = necwb::nec::NecModelConverter{}.convert(necwb::nec::NecParser{}.parse(
        "GH 8 10 0.05 0 0.1 0.1 0.2 0.2 0.001\n"
        "GW 1 3 1 -1 3 1 1 3 0.001\n"
        "GM 10 1 0 0 0 0 0 1\n"
        "GX 100 100\n"
        "GE 0\n"));
    const auto blocks = copied.model.segmentBlocks();
    const std::array expectedTags{8, 1, 18, 11, 108, 101, 118, 111};
    expect(copied.issues.empty() && copied.model.wireCount() == 4 && blocks.size() == 8
            && std::ranges::equal(blocks, expectedTags, {}, &necwb::model::SegmentBlock::tag)
            && necwb::model::referencedSegmentCount(copied.model, 0) == 52,
        "copied spirals keep their place and tag in NEC's segment order");
    const auto copiedWire = necwb::analysis::WireSegmentIndex(copied.model).locate(111, 51);
    expect(copiedWire && copiedWire->wireIndex == 3 && copiedWire->segment == 2,
        "segments after copied spirals map to the right drawn wire");

    const auto sharedWithSpiral = necwb::nec::NecModelConverter{}.convert(
        necwb::nec::NecParser{}.parse("GH 1 10 0.05 0 0.1 0.1 0.2 0.2 0.001\n"
                                      "GW 1 3 0 -1 3 0 1 3 0.001\nGE 0\n"));
    expect(necwb::model::hasSharedWireTags(sharedWithSpiral.model),
        "a spiral sharing a wire's tag counts as a shared tag");
}

void testGenericKnownCardValidation()
{
    const auto valid = necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
        "GA 1 9 1 0 180 .001\nGE 0\nPT 0 0 0 0\nEN\n"));
    expect(valid.errorCount() == 0,
        "recognized NEC-2 geometry and control cards accept correctly typed fixed fields");

    const auto invalid = necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
        "GA 1 9 bad 0 180 .001\nGE 0\nPT bad 0 0 0\nEN\n"));
    expect(invalid.errorCount() == 2,
        "recognized generic cards diagnose integer and floating-point field errors");
}

void testGeometryCardOrdering()
{
    const auto misplaced = necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
        "GN 2 0 0 0 13 0.005 0 0 0 0\n"
        "EX 0 1 1 0 1 0\n"
        "FR 0 1 0 0 14.175 0\n"
        "GW 1 11 -4.8768 0 6.096 4.8768 0 6.096 0.001\n"
        "XQ 0\nRP 0 37 36 1000 0 0 5 10 0 0\nEN\n"));
    expect(std::ranges::any_of(misplaced.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("GW geometry cards must precede") != std::string::npos;
    }), "geometry checker rejects GW cards after control cards");
    expect(std::ranges::any_of(misplaced.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("before GE") != std::string::npos;
    }), "geometry checker reports a missing GE boundary before control cards");

    const auto missingEnd = necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
        "GW 1 11 -1 0 0 1 0 0 .001\n"));
    expect(std::ranges::any_of(missingEnd.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("requires a GE card") != std::string::npos;
    }), "geometry checker requires GE after the final GW card");
}

void testDeckStructureValidation()
{
    const auto missingCommentEnd = necwb::nec::NecModelChecker{}.check(
        necwb::nec::NecParser{}.parse(
            "CMPP 3,0,0,0,0\n"
            "GW 1 41 -10 0 0 10 0 0 .001\nGE 0\n"
            "EX 0 1 21 0 1 0\nFR 0 1 0 0 28 0\nEN\n"));
    expect(missingCommentEnd.errorCount() == 1
            && missingCommentEnd.diagnostics.front().message.find("before CE")
                != std::string::npos,
        "a leading CM comment block requires CE before geometry");

    const auto missingEnd = necwb::nec::NecModelChecker{}.check(
        necwb::nec::NecParser{}.parse(
            "GW 1 41 -10 0 0 10 0 0 .001\nGE 0\n"
            "EX 0 1 21 0 1 0\nFR 0 1 0 0 28 0\n"));
    expect(missingEnd.errorCount() == 1
            && missingEnd.diagnostics.front().message.find("final EN")
                != std::string::npos,
        "a complete NEC deck requires an EN end card");

    const auto cardsAfterEnd = necwb::nec::NecModelChecker{}.check(
        necwb::nec::NecParser{}.parse(
            "GW 1 41 -10 0 0 10 0 0 .001\nGE 0\n"
            "EX 0 1 21 0 1 0\nFR 0 1 0 0 28 0\nEN\nPT -1\n"));
    expect(cardsAfterEnd.errorCount() == 1
            && cardsAfterEnd.diagnostics.back().message.find("EN must be the final")
                != std::string::npos,
        "EN must remain the final nonblank deck card");

    const auto corrected = necwb::nec::NecModelChecker{}.check(
        necwb::nec::NecParser{}.parse(
            "CMPP 3,0,0,0,0\nCE\n"
            "GW 1 41 -10 0 0 10 0 0 .001\nGE 0\n"
            "EX 0 1 21 0 1 0\nFR 0 1 0 0 28 0\nEN\n\n"));
    expect(corrected.errorCount() == 0,
        "a CE-terminated comment block and final EN pass structural validation");
}

void testControlCardSequence()
{
    const auto check = [](std::string_view controls) {
        return necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(
            "GW 1 9 0 0 0 0 0 5 .001\nGE 0\n" + std::string{controls} + "EN\n"));
    };
    const auto hasMessage = [](const auto& result, std::string_view text) {
        return std::ranges::any_of(result.diagnostics, [text](const auto& diagnostic) {
            return diagnostic.message.find(text) != std::string::npos;
        });
    };

    const auto patternThenDivider = check(
        "EX 0 1 5 0 1 0\nFR 0 3 0 0 14 0.1\n"
        "RP 0 19 1 1000 0 0 5 0\nXQ 0\n");
    expect(!hasMessage(patternThenDivider, "final frequency only"),
        "blank XQ after RP remains a legal NEC-2 execution-group divider");

    const auto sweepThenPattern = check(
        "EX 0 1 5 0 1 0\nFR 0 3 0 0 14 0.1\n"
        "XQ 0\nRP 0 19 1 1000 0 0 5 0\n");
    expect(hasMessage(sweepThenPattern, "final frequency only"),
        "a later RP without repeated FR reports NEC-2 final-frequency semantics");

    const auto repeatedSweep = check(
        "EX 0 1 5 0 1 0\nFR 0 3 0 0 14 0.1\nXQ 0\n"
        "FR 0 3 0 0 14 0.1\nRP 0 19 1 1000 0 0 5 0\n");
    expect(!hasMessage(repeatedSweep, "final frequency only"),
        "repeating FR starts a new automatic frequency loop");

    const auto missingExcitation = check(
        "FR 0 1 0 0 14 0\nRP 0 19 1 1000 0 0 5 0\nEX 0 1 5 0 1 0\n");
    expect(hasMessage(missingExcitation, "before an EX excitation"),
        "execution requests require the NEC-2 excitation exception to be satisfied");

    const auto unusedGround = check(
        "EX 0 1 5 0 1 0\nFR 0 1 0 0 14 0\nXQ 0\nGN 1\n");
    expect(hasMessage(unusedGround, "after the final execution request"),
        "a parameter change after the final execution request is rejected as unused");

    const auto repeatedExecution = check(
        "EX 0 1 5 0 1 0\nFR 0 1 0 0 14 0\nXQ 0\nGN 1\nXQ 0\n");
    expect(!hasMessage(repeatedExecution, "after the final execution request"),
        "a later execution request consumes changed calculation parameters");

    const auto unusedRadiationGround = check(
        "EX 0 1 5 0 1 0\nFR 0 1 0 0 14 0\nGD 0 0 0 0 13 0.005\nXQ 0\n");
    expect(hasMessage(unusedRadiationGround, "no later RP request"),
        "XQ does not consume GD parameters intended for radiated-field calculations");

    const auto pendingNearField = check(
        "EX 0 1 5 0 1 0\nFR 0 3 0 0 14 0.1\n"
        "NE 0 1 1 1 0 0 1 0 0 0\n");
    expect(hasMessage(pendingNearField, "follow it with XQ or RP"),
        "near-field requests in automatic stepping require a later execution card");

    const auto groupedNearField = check(
        "EX 0 1 5 0 1 0\nFR 0 3 0 0 14 0.1\n"
        "NE 0 1 1 1 0 0 1 0 0 0\nRP 0 19 1 1000 0 0 5 0\n");
    expect(!hasMessage(groupedNearField, "follow it with XQ or RP"),
        "RP executes a preceding near-field request during automatic stepping");
}

void testBackendCompatibilityProfiles()
{
    using necwb::nec::CompatibilityProfile;
    using necwb::nec::DiagnosticSeverity;
    using necwb::nec::ModelCheckOptions;

    expect(necwb::nec::compatibilityProfileForBackend("nec2")
            == CompatibilityProfile::OriginalNec2
            && necwb::nec::compatibilityProfileForBackend("nec2dxs")
                == CompatibilityProfile::FourNec2
            && necwb::nec::compatibilityProfileForBackend("opennec")
                == CompatibilityProfile::OpenNec,
        "solver backends map to their compatibility profiles");

    const auto deck = necwb::nec::NecParser{}.parse(
        "GW 1 9 0 0 0 0 0 5 .001\nGE 0\n"
        "EX 0 1 5 0 1 0\nFR 0 1 0 0 14.2 0\n"
        "LD 6 1 1 1 0.000001 0.00000000001 0\nXQ 0\nEN\n");
    const auto original = necwb::nec::NecModelChecker{}.check(deck,
        ModelCheckOptions{CompatibilityProfile::OriginalNec2});
    const auto fourNec2 = necwb::nec::NecModelChecker{}.check(deck,
        ModelCheckOptions{CompatibilityProfile::FourNec2});
    const auto openNec = necwb::nec::NecModelChecker{}.check(deck,
        ModelCheckOptions{CompatibilityProfile::OpenNec});
    const auto hasMessage = [](const auto& result, std::string_view text) {
        return std::ranges::any_of(result.diagnostics, [text](const auto& diagnostic) {
            return diagnostic.message.find(text) != std::string::npos;
        });
    };
    expect(original.errorCount() == 1
            && hasMessage(original, "Original NEC-2 and NEC2C support LD types 0 through 5"),
        "Original NEC-2 profile rejects the 4nec2 LD6 extension clearly");
    expect(fourNec2.errorCount() == 1
            && hasMessage(fourNec2, "does not yet convert it for direct NEC2dXS execution"),
        "4nec2 profile recognizes LD6 but blocks the unimplemented direct-solver conversion");
    expect(openNec.errorCount() == 0,
        "OpenNEC profile accepts its supported 4nec2 LD6 extension");

    const auto symbolInfo = necwb::nec::NecModelChecker{}.check(deck,
        ModelCheckOptions{CompatibilityProfile::OpenNec, 2});
    expect(symbolInfo.informationCount() == 1
            && std::ranges::any_of(symbolInfo.diagnostics, [](const auto& diagnostic) {
                return diagnostic.severity == DiagnosticSeverity::Information
                    && diagnostic.message.find("2 SY symbols") != std::string::npos;
            }),
        "resolved SY symbols are reported as a non-blocking generated-deck conversion");
}

void testIncompleteModelCheck()
{
    const auto result = necwb::nec::NecModelChecker{}.check(
        necwb::nec::NecParser{}.parse("CM incomplete model\nCE\nEN\n"));
    expect(result.errorCount() == 0,
        "incomplete analysis setup remains editable rather than becoming a syntax error");
    expect(result.warningCount() == 3,
        "missing geometry, frequency, and source produce completeness warnings");
    expect(result.diagnostics[0].message.find("no valid geometry") != std::string::npos,
        "incomplete model warning identifies missing geometry");
    expect(result.diagnostics[1].message.find("no supported FR") != std::string::npos,
        "incomplete setup warning identifies missing frequency");
    expect(result.diagnostics[2].message.find("no supported EX") != std::string::npos,
        "incomplete setup warning identifies missing source");
}

void testOrthographicProjection()
{
    const necwb::model::Point3D point{1.0, 2.0, 3.0};
    expect(necwb::geometry::project(point, necwb::geometry::ProjectionPlane::XY)
            == necwb::geometry::Point2D{1.0, 2.0},
        "XY projection maps X and Y");
    expect(necwb::geometry::project(point, necwb::geometry::ProjectionPlane::XZ)
            == necwb::geometry::Point2D{1.0, 3.0},
        "XZ projection maps X and Z");
    expect(necwb::geometry::project(point, necwb::geometry::ProjectionPlane::YZ)
            == necwb::geometry::Point2D{2.0, 3.0},
        "YZ projection maps Y and Z");
    expect(necwb::geometry::withProjectedCoordinates(point, {8.0, 9.0},
            necwb::geometry::ProjectionPlane::XY) == necwb::model::Point3D{8.0, 9.0, 3.0},
        "XY editing preserves Z");
    expect(necwb::geometry::withProjectedCoordinates(point, {8.0, 9.0},
            necwb::geometry::ProjectionPlane::XZ) == necwb::model::Point3D{8.0, 2.0, 9.0},
        "XZ editing preserves Y");
    expect(necwb::geometry::withProjectedCoordinates(point, {8.0, 9.0},
            necwb::geometry::ProjectionPlane::YZ) == necwb::model::Point3D{1.0, 8.0, 9.0},
        "YZ editing preserves X");
}

void testWireLookupAndWriting()
{
    necwb::model::AntennaModel model;
    model.addWire({7, {-5.0, 0.0, 10.0}, {5.0, 0.0, 10.0}, 21, 0.001, 3});
    auto* wire = model.wireByTag(7);
    expect(wire != nullptr, "wire lookup finds a mutable wire");
    wire->end.z = 12.5;
    expect(model.wireByTag(7)->end.z == 12.5, "mutable wire lookup updates the model");
    expect(necwb::nec::NecWriter{}.writeWireCard(*wire)
            == "GW 7 21 -5 0 10 5 0 12.5 0.001",
        "wire writer generates a parseable GW card");
}

void testProjectionBoundsAndDistance()
{
    necwb::model::AntennaModel model;
    model.addWire({1, {-2.0, 1.0, 0.0}, {4.0, 3.0, 0.0}, 7, 0.001, 1});
    model.addWire({2, {0.0, -5.0, 2.0}, {0.0, 2.0, 4.0}, 5, 0.001, 2});
    const auto bounds = necwb::geometry::projectedBounds(model.wires(), necwb::geometry::ProjectionPlane::XY);
    expect(bounds.has_value(), "non-empty model has projected bounds");
    expect(bounds->minimumHorizontal == -2.0 && bounds->maximumHorizontal == 4.0,
        "projection bounds include horizontal endpoints");
    expect(bounds->minimumVertical == -5.0 && bounds->maximumVertical == 3.0,
        "projection bounds include vertical endpoints");
    const auto xzBounds = necwb::geometry::projectedBounds(model.wires(), necwb::geometry::ProjectionPlane::XZ);
    expect(xzBounds->minimumHorizontal == -2.0 && xzBounds->maximumHorizontal == 4.0
            && xzBounds->minimumVertical == 0.0 && xzBounds->maximumVertical == 4.0,
        "XZ bounds use X and Z coordinates");
    const auto yzBounds = necwb::geometry::projectedBounds(model.wires(), necwb::geometry::ProjectionPlane::YZ);
    expect(yzBounds->minimumHorizontal == -5.0 && yzBounds->maximumHorizontal == 3.0
            && yzBounds->minimumVertical == 0.0 && yzBounds->maximumVertical == 4.0,
        "YZ bounds use Y and Z coordinates");
    expect(necwb::geometry::distanceToSegment({0.0, 2.0}, {0.0, 0.0}, {4.0, 0.0}) == 2.0,
        "segment distance uses nearest projected point");
    expect(necwb::geometry::distanceToSegment({3.0, 4.0}, {0.0, 0.0}, {0.0, 0.0}) == 5.0,
        "point projection distance handles zero-length projections");
    expect(necwb::geometry::closestSegmentParameter({2.0, 3.0}, {0.0, 0.0}, {4.0, 0.0}) == 0.5,
        "segment parameter locates a projected split point");
    expect(necwb::geometry::closestSegmentParameter({8.0, 0.0}, {0.0, 0.0}, {4.0, 0.0}) == 1.0,
        "segment parameter clamps beyond an endpoint");
}

void testLengthUnitConversions()
{
    using necwb::model::LengthUnit;
    expect(necwb::model::toMeters(100.0, LengthUnit::Centimeter) == 1.0,
        "centimeters convert to meters");
    expect(necwb::model::fromMeters(0.0254, LengthUnit::Inch) == 1.0,
        "meters convert to inches");
    expect(necwb::model::toMeters(1.0, LengthUnit::Foot) == 0.3048,
        "feet convert to meters");
    expect(necwb::model::lengthUnitSymbol(LengthUnit::Millimeter) == "mm",
        "length units provide display symbols");
    expect(necwb::model::niceEngineeringStep(4.2) == 5.0,
        "automatic grids choose a clean five-unit step");
    expect(necwb::model::niceEngineeringStep(5.1) == 10.0,
        "automatic grids advance to the next engineering step");
    expect(necwb::model::nextEngineeringStep(0.5) == 1.0,
        "friendly snap increment advances from one-half to one");
    expect(necwb::model::previousEngineeringStep(0.5) == 0.2,
        "friendly snap decrement moves from one-half to one-fifth");
    expect(necwb::model::previousEngineeringStep(1.0) == 0.5,
        "friendly snap decrement crosses an engineering decade");
    expect(necwb::model::niceEngineeringStep(necwb::model::fromMeters(
               necwb::model::toMeters(1.0, LengthUnit::Foot), LengthUnit::Meter))
            == 0.5,
        "unit-friendly snapping converts one foot to a clean half-meter interval");
}

void testWireGaugeConversions()
{
    expect(necwb::model::isValidAwg(-3) && necwb::model::isValidAwg(40),
        "AWG range includes 4/0 through 40 gauge");
    expect(!necwb::model::isValidAwg(-4) && !necwb::model::isValidAwg(41),
        "AWG range rejects unsupported sizes");
    expect(std::abs(necwb::model::awgDiameterMeters(18) - 0.001023687) < 0.00000001,
        "18 AWG uses the standard nominal diameter");
    expect(necwb::model::awgRadiusMeters(18) == necwb::model::awgDiameterMeters(18) / 2.0,
        "AWG radius is half the nominal diameter");
    expect(necwb::model::awgLabel(-3) == "4/0 AWG" && necwb::model::awgLabel(0) == "1/0 AWG",
        "large AWG sizes use conventional labels");
    expect(necwb::model::matchingAwg(necwb::model::awgRadiusMeters(12)) == 12,
        "standard AWG radii can be recognized for property display");
    expect(!necwb::model::matchingAwg(0.001).has_value(),
        "custom NEC radii remain custom instead of being silently rounded");
}

void testAutoSegmentation()
{
    necwb::model::AntennaModel model;
    model.addWire({1, {0.0, 0.0, -0.5}, {0.0, 0.0, 0.5}, 11, 0.001, 1});
    model.addWire({2, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 10, 0.001, 2});
    const std::vector<necwb::model::Excitation> excitations{{0, 1, 6, 1.0, 0.0, 3}};
    const auto proposal = necwb::model::proposeSegmentation(model, excitations, 300.0, {20, true});
    expect(proposal.wires.size() == 2, "auto segmentation proposes every wire");
    expect(proposal.wires[0].newSegments == 21,
        "excited wire receives an odd wavelength-based segment count");
    expect(proposal.wires[1].newSegments == 21,
        "one-meter wire at 300 MHz rounds wavelength recommendation upward");
    expect(proposal.remappedExcitations[0].segment == 11,
        "center source remains centered after automatic segmentation");
    const auto evenProposal = necwb::model::proposeSegmentation(model, excitations, 299.792458, {20, false});
    expect(evenProposal.wires[0].newSegments == 20,
        "odd-count preference can be disabled");

    necwb::model::AntennaModel transmissionLineModel;
    transmissionLineModel.addWire(
        {1, {-5.03, 0.0, 7.62}, {5.03, 0.0, 7.62}, 21, 0.001, 1});
    transmissionLineModel.addWire(
        {2, {0.0, 0.0, 100.0}, {0.0, 0.0, 100.001}, 1, 0.000001, 2});
    const std::vector<necwb::model::Excitation> remoteExcitation{
        {0, 2, 1, 1.0, 0.0, 11}};
    const std::vector<necwb::model::LoadDefinition> loads{
        {4, 1, 5, 17, 50.0, 0.0, 0.0, 10}};
    const std::vector<necwb::model::TransmissionLineDefinition> lines{
        {1, 11, 2, 1, 50.0, 5.0, 0.0, 0.0, 0.0, 0.0, 9}};
    const auto attachmentProposal = necwb::model::proposeSegmentation(
        transmissionLineModel, remoteExcitation, 14.25, {20, true}, loads, lines);
    expect(attachmentProposal.wires[0].newSegments == 11,
        "a TL-fed wire keeps an odd center segment during automatic segmentation");
    expect(attachmentProposal.remappedTransmissionLines[0].segment1 == 6
            && attachmentProposal.remappedTransmissionLines[0].segment2 == 1,
        "automatic segmentation remaps both transmission-line endpoints");
    expect(attachmentProposal.remappedLoads[0].firstSegment == 3
            && attachmentProposal.remappedLoads[0].lastSegment == 9,
        "automatic segmentation remaps supported load ranges");

    const auto transmissionLineDeck = necwb::nec::NecParser{}.parse(
        "CM 20m dipole at 25 ft\nCE\n"
        "GW 1 21 -5.030 0.000 7.620 5.030 0.000 7.620 0.001\n"
        "GW 2 1 0.000 0.000 100.000 0.000 0.000 100.001 0.000001\n"
        "GE 0\nGN 2 0 0 0 13.000 0.005\nFR 0 26 0 0 14 0.01\n"
        "TL 1 11 2 1 50.000 5 0.000 0.000 0.000 0.000\n"
        "EX 0 2 1 0 1 0\nRP 0 19 37 1000 0.000 0.000 5.000 10.000\nEN\n");
    expect(necwb::nec::NecModelChecker{}.check(transmissionLineDeck).errorCount() == 0,
        "valid EX 0 and TL references pass model checking");
}

void testStructuredModelSetup()
{
    const std::string source =
        "GW 7 21 -5 0 10 5 0 10 .001\n"
        "GE\n"
        "EX 0 7 11 0 0 2\n"
        "FR 0 3 0 0 14 0.25\n"
        "EN\n";
    const auto document = necwb::nec::NecParser{}.parse(source);
    const auto setup = necwb::nec::NecSetupConverter{}.convert(document);
    expect(setup.frequency && setup.frequency->count == 3 && setup.frequency->startMHz == 14.0,
        "structured setup reads the first frequency card");
    expect(setup.excitations.size() == 1 && setup.excitations.front().wireTag == 7,
        "structured setup reads voltage-source wire references");
    expect(std::abs(setup.excitations.front().magnitude - 2.0) < 1.0e-12
            && std::abs(setup.excitations.front().phaseDegrees - 90.0) < 1.0e-12,
        "EX real and imaginary values convert to magnitude and phase");
    const auto model = necwb::nec::NecModelConverter{}.convert(document).model;
    const auto position = necwb::model::excitationPosition(model, setup.excitations.front());
    expect(position && std::abs(position->x) < 1.0e-12,
        "source markers use the center of the referenced segment");
    expect(necwb::nec::NecWriter{}.writeFrequencyCard(*setup.frequency) == "FR 0 3 0 0 14 0.25",
        "frequency definitions write canonical FR cards");
    expect(necwb::nec::NecModelChecker{}.check(document).errorCount() == 0,
        "valid structured setup passes model checking");
    expect(necwb::model::frequencyPointCount(0, 10.0, 20.0, 2.0) == 6,
        "linear sweep count includes both exact endpoints");
    expect(necwb::model::frequencyPointCount(0, 10.0, 20.0, 3.0) == 4,
        "linear sweep count does not exceed the requested end");
    expect(necwb::model::frequencyPointCount(0, 14.0, 14.350, 0.01) == 36,
        "14.000 through 14.350 MHz at 0.010 MHz includes all 36 points");
    expect(necwb::model::frequencyPointCount(1, 1.0, 16.0, 2.0) == 5,
        "multiplicative sweep count spans powers through the endpoint");
    expect(!necwb::model::frequencyPointCount(1, 1.0, 16.0, 1.0),
        "multiplicative sweep requires a multiplier above one");
    expect(necwb::model::frequencyEndMHz({0, 4, 10.0, 3.0, 0}) == 19.0,
        "frequency endpoint is derived from NEC linear count and step");

    const auto invalid = necwb::nec::NecParser{}.parse(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nEX 0 2 1 0 1 0\nFR 0 2 0 0 10 0\nEN\n");
    expect(necwb::nec::NecModelChecker{}.check(invalid).errorCount() == 2,
        "invalid source references and zero frequency steps are diagnosed");

    const auto fullWidthFrequency = necwb::nec::NecParser{}.parse(
        "FR 0 101 0 0 3.00000E+00 5.00000E-02 8.00000E+00 0.00000E+00 0.00000E+00 0.00000E+00\n"
        "EN\n");
    const auto fullWidthSetup = necwb::nec::NecSetupConverter{}.convert(fullWidthFrequency);
    expect(necwb::nec::NecModelChecker{}.check(fullWidthFrequency).errorCount() == 0,
        "full-width FR cards accept all six floating-point fields");
    expect(fullWidthSetup.frequency && fullWidthSetup.frequency->count == 101
            && fullWidthSetup.frequency->startMHz == 3.0
            && fullWidthSetup.frequency->step == 0.05,
        "structured setup uses the meaningful FR fields and ignores valid trailing fields");
}

void testGroundSetup()
{
    const auto document = necwb::nec::NecParser{}.parse(
        "GW 1 9 0 0 0 0 0 5 .001\n"
        "GE 1\n"
        "GN 2 0 0 0 13 0.005 0 0 0 0\n"
        "EX 0 1 5 0 1 0\n"
        "EN\n");
    const auto setup = necwb::nec::NecSetupConverter{}.convert(document);
    expect(setup.ground && setup.ground->type == necwb::model::GroundType::SommerfeldNorton,
        "structured setup reads Sommerfeld/Norton ground");
    expect(setup.ground && setup.ground->relativePermittivity == 13.0
            && setup.ground->conductivity == 0.005
            && setup.ground->geometryGroundFlag == 1,
        "ground material and GE connection flag are preserved");
    expect(necwb::nec::NecWriter{}.writeGroundCard(*setup.ground)
            == "GN 2 0 0 0 13 0.005 0 0 0 0",
        "real ground writes a canonical full-width GN card");
    expect(necwb::nec::NecWriter{}.writeGeometryEndCard(-1) == "GE -1",
        "geometry ground handling writes the GE flag");
    expect(necwb::nec::NecModelChecker{}.check(document).errorCount() == 0,
        "valid real-ground setup passes model checking");

    const auto invalid = necwb::nec::NecParser{}.parse(
        "GE 1\nGN 0 0 0 0 -2 -0.01\nEN\n");
    expect(necwb::nec::NecModelChecker{}.check(invalid).errorCount() == 1,
        "invalid real-ground material values are diagnosed");
}

void testAnalysisRequests()
{
    const auto document = necwb::nec::NecParser{}.parse(
        "GW 1 9 0 0 0 0 0 5 .001\n"
        "GE 0\n"
        "EX 0 1 5 0 1 0\n"
        "FR 0 1 0 0 14.2 0\n"
        "XQ 0\n"
        "RP 0 91 1 1000 0 0 1 0 0 0\n"
        "RP 0 1 360 1000 62 0 0 1 0 0\n"
        "Z0 450 0 0 0 0 0 0 0 0 0\n"
        "EN\n");
    const auto setup = necwb::nec::NecSetupConverter{}.convert(document);
    expect(setup.executionRequest && setup.executionRequest->option == 0,
        "structured setup reads XQ execution requests");
    expect(setup.radiationPatterns.size() == 2
            && setup.radiationPatterns[0].thetaCount == 91
            && setup.radiationPatterns[0].phiCount == 1
            && setup.radiationPatterns[0].thetaStep == 1.0
            && setup.radiationPatterns[1].thetaStart == 62.0
            && setup.radiationPatterns[1].phiCount == 360,
        "structured setup retains every RP request");
    expect(setup.referenceImpedance && setup.referenceImpedance->ohms == 450.0
            && necwb::model::referenceImpedanceOhms(setup) == 450.0,
        "structured setup reads xnec2c reference impedance metadata");
    expect(necwb::nec::NecWriter{}.writeExecutionCard(*setup.executionRequest) == "XQ 0",
        "execution requests write canonical XQ cards");
    expect(necwb::nec::NecWriter{}.writeRadiationPatternCard(setup.radiationPatterns[0])
            == "RP 0 91 1 1000 0 0 1 0 0 0",
        "radiation requests write canonical RP cards");
    expect(necwb::nec::NecModelChecker{}.check(document).errorCount() == 0,
        "valid XQ and RP requests pass model checking");

    const auto shortPattern = necwb::nec::NecParser{}.parse(
        "GW 1 9 0 0 0 0 0 5 .001\nGE 0\n"
        "EX 0 1 5 0 1 0\nFR 0 1 0 0 14.2 0\n"
        "RP 0 181 361 1000 0.000 0.000 1.000 1.000\nEN\n");
    const auto shortSetup = necwb::nec::NecSetupConverter{}.convert(shortPattern);
    expect(necwb::nec::NecModelChecker{}.check(shortPattern).errorCount() == 0
            && shortSetup.radiationPatterns.size() == 1
            && shortSetup.radiationPatterns[0].thetaCount == 181
            && shortSetup.radiationPatterns[0].phiCount == 361,
        "RP accepts omitted optional distance and normalization fields");

    const auto invalid = necwb::nec::NecParser{}.parse(
        "GW 1 9 0 0 0 0 0 5 .001\nGE 0\n"
        "EX 0 1 5 0 1 0\nFR 0 1 0 0 14.2 0\n"
        "RP 0 0 2 1000 0 0 1 0 0 0\nXQ bad\nZO -50\nEN\n");
    expect(necwb::nec::NecModelChecker{}.check(invalid).errorCount() == 4,
        "invalid RP counts, XQ options, and reference impedance are diagnosed");
}

void testLoadsAndTransmissionLines()
{
    const auto document = necwb::nec::NecParser{}.parse(
        "GW 1 9 0 0 0 0 0 1 .001\nGW 2 9 1 0 0 1 0 1 .001\nGE 0\n"
        "LD 4 1 3 5 50 12 0\nTL 1 5 2 5 50 .25 0 0 0 0\n"
        "EX 0 1 5 0 1 0\nFR 0 1 0 0 14 0\nEN\n");
    const auto setup = necwb::nec::NecSetupConverter{}.convert(document);
    expect(setup.loads.size() == 1 && setup.loads[0].type == 4
            && setup.loads[0].firstSegment == 3 && setup.loads[0].value2 == 12.0,
        "structured setup reads LD cards");
    expect(setup.transmissionLines.size() == 1
            && setup.transmissionLines[0].wireTag2 == 2
            && setup.transmissionLines[0].characteristicImpedance == 50.0,
        "structured setup reads TL cards");
    expect(necwb::nec::NecWriter{}.writeLoadCard(setup.loads[0]) == "LD 4 1 3 5 50 12 0",
        "load definitions write canonical LD cards");
    expect(necwb::nec::NecWriter{}.writeTransmissionLineCard(setup.transmissionLines[0])
            == "TL 1 5 2 5 50 0.25 0 0 0 0",
        "transmission line definitions write canonical TL cards");
    expect(necwb::nec::NecModelChecker{}.check(document).errorCount() == 0,
        "valid LD and TL cards pass model checking");
    const auto model = necwb::nec::NecModelConverter{}.convert(document).model;
    const auto loadPosition = necwb::model::loadPosition(model, setup.loads[0]);
    const auto lineEndpoint = necwb::model::wireSegmentPosition(model, 2, 5);
    expect(loadPosition && std::abs(loadPosition->z - 3.5 / 9.0) < 1.0e-12,
        "load markers use the center of their segment range");
    expect(lineEndpoint && lineEndpoint->x == 1.0
            && std::abs(lineEndpoint->z - 0.5) < 1.0e-12,
        "transmission-line endpoints use segment centers");
    const auto invalidLine = necwb::nec::NecParser{}.parse(
        "GW 1 9 0 0 0 0 0 1 .001\nGE 0\nTL 1 1 1 9 0 0 0 0 0 0\nEN\n");
    expect(necwb::nec::NecModelChecker{}.check(invalidLine).errorCount() == 1,
        "zero-impedance transmission lines are rejected");

    const auto omittedTrailingValues = necwb::nec::NecParser{}.parse(
        "GW 1 9 0 0 0 0 0 1\nGC 0 0 1 .001 .001\n"
        "GW 2 9 1 0 0 1 0 1 .001\nGE 0\n"
        "LD 5 0 0 0 3.72e7\nTL 1 5 2 5 50\n"
        "EX 0 1 5 0 1\nFR 0 1 0 0 14\nEN\n");
    const auto omittedSetup = necwb::nec::NecSetupConverter{}.convert(omittedTrailingValues);
    const auto omittedModel = necwb::nec::NecModelConverter{}.convert(omittedTrailingValues);
    expect(necwb::nec::NecModelChecker{}.check(omittedTrailingValues).errorCount() == 0,
        "valid omitted trailing zero fields pass model checking");
    expect(omittedModel.issues.empty() && omittedModel.model.wireCount() == 2,
        "GW may omit its radius when a following GC supplies taper radii");
    expect(omittedSetup.excitations.size() == 1
            && omittedSetup.excitations[0].phaseDegrees == 0.0
            && omittedSetup.loads.size() == 1
            && omittedSetup.loads[0].wireTag == 0
            && omittedSetup.loads[0].value2 == 0.0
            && omittedSetup.loads[0].value3 == 0.0
            && omittedSetup.transmissionLines.size() == 1
            && omittedSetup.transmissionLines[0].lengthMeters == 0.0
            && omittedSetup.transmissionLines[0].shuntReal1 == 0.0
            && omittedSetup.transmissionLines[0].shuntImaginary2 == 0.0,
        "omitted EX, LD, and TL trailing fields convert to zero defaults");
}

void testSolverCommand()
{
    expect(necwb::analysis::isBackendRunnable("nec2"),
        "nec2 has a process adapter");
    expect(necwb::analysis::isBackendRunnable("opennec")
            && necwb::analysis::isBackendRunnable("nec2dxs")
            && !necwb::analysis::isBackendRunnable("unknown"),
        "OpenNEC and NEC2dXS have explicit process adapters");
    const auto command = necwb::analysis::buildSolverCommand(
        "nec2", "/usr/bin/nec2c", "model.nec", "model.out");
    expect(command.executable == "/usr/bin/nec2c",
        "solver command retains the configured executable");
    expect(command.arguments.size() == 2
            && command.arguments[0] == "-imodel.nec"
            && command.arguments[1] == "-omodel.out",
        "nec2c command uses short working-directory-relative arguments");
    expect(command.standardInput.empty(),
        "nec2c does not require redirected filename input");

    const auto openNec = necwb::analysis::buildSolverCommand(
        "opennec", "onec.exe", "model.nec", "model.out");
    expect(openNec.arguments == std::vector<std::string>(
                {"-f", "original", "-o", "model.out", "model.nec"})
            && openNec.standardInput.empty(),
        "OpenNEC requests original-format output with positional input");

    const auto nec2dXs = necwb::analysis::buildSolverCommand(
        "nec2dxs", "Nec2dXS1k5.exe", "model.nec", "model.out");
    expect(nec2dXs.arguments.empty()
            && nec2dXs.standardInput == "model.nec\nmodel.out\n",
        "NEC2dXS receives input and output filenames through standard input");
    expect(necwb::analysis::solverSegmentCapacity(
               "nec2dxs", "C:/4nec2/exe/Nec2dXS1k5.exe") == 1500
            && necwb::analysis::solverSegmentCapacity(
                   "nec2dxs", "C:/4nec2/exe/Nec2dXS11k.exe") == 11000
            && !necwb::analysis::solverSegmentCapacity(
                   "nec2dxs", "C:/custom/renamed-engine.exe")
            && !necwb::analysis::solverSegmentCapacity(
                   "opennec", "C:/4nec2/exe/Nec2dXS1k5.exe"),
        "NEC2dXS capacity is inferred only from recognized backend executable names");
}

void testRadiationSweepSolverInput()
{
    const auto normalizedWithoutCe = necwb::analysis::normalizeSolverDeck(
        "CM opening note\nGW 1 1 0 0 0 1 0 0 .001\nCM inline note\nGE 0\nEN\n");
    expect(normalizedWithoutCe.find("CM opening note") != std::string::npos
            && normalizedWithoutCe.find("CM inline note") == std::string::npos,
        "solver normalization recognizes inline comments even without an explicit CE card");
    const std::string source =
        "CM sweep\nCE\nCM inline geometry note\nGW 1 11 0 0 0 1 0 0 .001\nGE 0\n"
        "EX 0 1 6 0 1 0\nFR 0 3 0 0 14 0.25\nGN 2 0 0 0 13 .005\nXQ 0\n"
        "RP 0 19 12 1000 0 0 10 30 0 0\n"
        "RP 0 1 360 1000 62 0 0 1 0 0\nZ0 450\nEN\n";
    const auto document = necwb::nec::NecParser{}.parse(source);
    const auto setup = necwb::nec::NecSetupConverter{}.convert(document);
    const auto countOccurrences = [](const std::string& text, std::string_view value) {
        auto count = 0;
        auto position = std::size_t{};
        while ((position = text.find(value, position)) != std::string::npos) {
            ++count;
            position += value.size();
        }
        return count;
    };
    const auto prepared = necwb::analysis::prepareSolverInput(source, setup);
    expect(prepared.find("CM sweep") != std::string::npos
            && prepared.find("CM inline geometry note") == std::string::npos
            && prepared.find("Z0 450") == std::string::npos,
        "solver input retains opening comments and omits inline comments and xnec2c metadata");
    expect(prepared.find("GN 2 0 0 0 13 .005\nFR 0 3 0 0 14 0.25\nXQ 0") != std::string::npos,
        "center-only radiation retains the fast native impedance sweep");
    expect(countOccurrences(prepared, "RP 0 19 12 1000") == 1
            && countOccurrences(prepared, "RP 0 1 360 1000") == 1
            && prepared.find("FR 0 1 0 0 14.25 0\nRP") != std::string::npos,
        "center-only radiation retains all RP requests at the sweep midpoint");

    const auto representative = necwb::analysis::prepareSolverInput(source, setup,
        necwb::analysis::RadiationSweepMode::RepresentativeFrequencies);
    expect(countOccurrences(representative, "RP 0 19 12 1000") == 3
            && countOccurrences(representative, "RP 0 1 360 1000") == 3
            && representative.find("FR 0 1 0 0 14 0\nRP") != std::string::npos
            && representative.find("FR 0 1 0 0 14.25 0\nRP") != std::string::npos
            && representative.find("FR 0 1 0 0 14.5 0\nRP") != std::string::npos,
        "representative radiation requests start, center, and end patterns");

    const necwb::analysis::FrequencyPlan selectedPatternFrequencies{
        necwb::analysis::FrequencyPlanMode::Explicit, {14.1, 14.4}, {}};
    const auto selectedPatterns = necwb::analysis::prepareSolverInput(
        source, setup, selectedPatternFrequencies);
    expect(countOccurrences(selectedPatterns, "FR 0 3 0 0 14 0.25") == 1
            && countOccurrences(selectedPatterns, "RP 0 19 12 1000") == 2
            && countOccurrences(selectedPatterns, "RP 0 1 360 1000") == 2
            && selectedPatterns.find("FR 0 1 0 0 14.1 0\nRP") != std::string::npos
            && selectedPatterns.find("FR 0 1 0 0 14.4 0\nRP") != std::string::npos,
        "selected pattern frequencies retain impedance sweep and repeat every RP request");

    const necwb::analysis::FrequencyPlan continuousPatternFrequencies{
        necwb::analysis::FrequencyPlanMode::Explicit, {}, {{14.0, 14.1, 0.05}}};
    const auto continuousPatterns = necwb::analysis::prepareSolverInput(
        source, setup, continuousPatternFrequencies);
    expect(countOccurrences(continuousPatterns, "RP 0 19 12 1000") == 3
            && continuousPatterns.find("FR 0 1 0 0 14.05 0\nRP") != std::string::npos,
        "continuous pattern frequency range expands through the shared frequency plan");

    const std::string sourceWithoutExecution =
        "CM sweep\nCE\nGW 1 11 0 0 0 1 0 0 .001\nGE 0\n"
        "EX 0 1 6 0 1 0\nFR 0 36 0 0 14 0.01\nGN 2 0 0 0 13 .005\n"
        "RP 0 19 12 1000 0 0 10 30 0 0\nEN\n";
    const auto setupWithoutExecution = necwb::nec::NecSetupConverter{}.convert(
        necwb::nec::NecParser{}.parse(sourceWithoutExecution));
    const auto representativeWithoutExecution = necwb::analysis::prepareSolverInput(
        sourceWithoutExecution, setupWithoutExecution,
        necwb::analysis::RadiationSweepMode::RepresentativeFrequencies);
    expect(representativeWithoutExecution.find("FR 0 36 0 0 14 0.01\nXQ 0")
                != std::string::npos
            && countOccurrences(representativeWithoutExecution, "RP 0 19 12 1000") == 3,
        "representative radiation preserves the complete impedance sweep without an authored XQ");

    const auto everyFrequency = necwb::analysis::prepareSolverInput(source, setup,
        necwb::analysis::RadiationSweepMode::EveryFrequency);
    expect(countOccurrences(everyFrequency, "FR 0 3 0 0 14 0.25") == 1
            && countOccurrences(everyFrequency, "XQ 0") == 1
            && countOccurrences(everyFrequency, "RP 0 19 12 1000") == 1
            && countOccurrences(everyFrequency, "RP 0 1 360 1000") == 1,
        "every-frequency radiation preserves the compact native NEC sweep");

    const std::string logarithmicSource =
        "GW 1 11 0 0 0 1 0 0 .001\nGE 0\nEX 0 1 6 0 1 0\n"
        "FR 1 3 0 0 10 2\nRP 0 19 12 1000 0 0 10 30 0 0\nEN\n";
    const auto logarithmicSetup = necwb::nec::NecSetupConverter{}.convert(
        necwb::nec::NecParser{}.parse(logarithmicSource));
    const auto logarithmicPrepared = necwb::analysis::prepareSolverInput(
        logarithmicSource, logarithmicSetup,
        necwb::analysis::RadiationSweepMode::EveryFrequency);
    expect(countOccurrences(logarithmicPrepared, "FR 1 3 0 0 10 2") == 1
            && countOccurrences(logarithmicPrepared, "RP 0 19 12 1000") == 1,
        "every-frequency multiplicative radiation also remains a native sweep");

    const std::string xnecSweep =
        "CM xnec2c sweep\nCE\nGW 1 11 0 0 0 0 0 12 1.25000E-03\nGE 1\n"
        "EX 0 1 1 0 1.00000E+00 0\nFR 0 55 0 0 3.00000E+00 5.00000E-01\n"
        "RP 0 19 37 1000 0 0 5 10 0 0\nGN 0 16 0 0 1.20000E+01 5.00000E-02 20 0.005\n"
        "ZO 450 0 0 0 0 0 0 0 0 0\nEN 0 0 0 0 0 0 0 0 0 0\n";
    const auto xnecSetup = necwb::nec::NecSetupConverter{}.convert(
        necwb::nec::NecParser{}.parse(xnecSweep));
    const auto xnecPrepared = necwb::analysis::prepareSolverInput(
        xnecSweep, xnecSetup, necwb::analysis::RadiationSweepMode::EveryFrequency);
    expect(countOccurrences(xnecPrepared, "FR 0 55") == 1
            && countOccurrences(xnecPrepared, "RP 0 19 37") == 1
            && xnecPrepared.find("ZO 450") == std::string::npos
            && xnecPrepared.find("RP 0 19 37") < xnecPrepared.find("GN 0 16"),
        "solver normalization keeps card order, compact FR sweep, and strips legacy ZO metadata");

    const std::string singleFrequencySource =
        "CM single\nCE\nCM inline note\nGW 1 11 0 0 0 1 0 0 .001\nGE 0\n"
        "EX 0 1 6 0 1 0\nFR 0 1 0 0 14 0\nEN\n";
    const auto singleFrequencySetup = necwb::nec::NecSetupConverter{}.convert(
        necwb::nec::NecParser{}.parse(singleFrequencySource));
    const auto singleFrequencyPrepared = necwb::analysis::prepareSolverInput(
        singleFrequencySource, singleFrequencySetup);
    expect(singleFrequencyPrepared.find("CM inline note") == std::string::npos
            && singleFrequencyPrepared.find("GW 1 11") != std::string::npos,
        "single-frequency solver input also omits inline comments after CE");
}

void testNecOutputParsing()
{
    const std::string output =
        " --------- FREQUENCY --------\n"
        " FREQUENCY : 1.4000E+01 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " TAG SEG headers\n"
        " 1 6 1.0E+00 0.0E+00 1.0E-02 -2.0E-03 7.5E+01 1.5E+01 0 0 5.0E-03\n"
        "\n"
        " FREQUENCY : 1.4100D+01 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 6 1.0D+00 0.0D+00 1.1D-02 0.0D+00 5.0D+01 0.0D+00 0 0 5.5D-03\n"
        "\n"
        " CURRENTS AND LOCATION\n"
        " 1 1 0 0 -0.1 0.05 1.0E-02 -2.0E-03 1.0198E-02 -11.31\n"
        " 2 1 0 0 0.1 0.05 8.0E-03 1.0E-03 8.0623E-03 7.125\n"
        " 3 2 0.2 0 0.1 0.05 -7.0E-03 1.0E-03 7.0711E-03 171.87\n"
        "\n"
        " FREQUENCY : 1.4100E+01 MHz\n"
        " ANTENNA INPUT PARAMETERS\n"
        " 1 6 1.0E+00 0.0E+00 1.1E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.5E-03\n"
        "\n"
        " CURRENTS AND LOCATION\n"
        " 1 1 0 0 -0.1 0.05 1.0E-02 -2.0E-03 1.0198E-02 -11.31\n"
        " 2 1 0 0 0.1 0.05 8.0E-03 1.0E-03 8.0623E-03 7.125\n"
        " 3 2 0.2 0 0.1 0.05 -7.0E-03 1.0E-03 7.0711E-03 171.87\n"
        "\n"
        " RADIATION PATTERNS\n"
        " 0 0 -999.99 -999.99 -999.99 0 0\n"
        " 90 0 2.15 -999.99 2.15 0 0 LINEAR\n"
        " 90 180 0 0 -7.85 1 45 RIGHT\n"
        "\n"
        " FREQUENCY : 1.4200E+01 MHz\n"
        " RADIATION PATTERNS\n"
        " 90 0 2.25 -999.99 2.25 0 0 LINEAR\n"
        "\n"
        " RADIATION PATTERNS\n"
        " 62 0 1.25 -999.99 1.25 0 0 LINEAR\n"
        "\n"
        " RADIATION PATTERNS\n"
        "\n"
        " FREQUENCY= 1.4300E+01 MHZ\n"
        " ANTENNA INPUT PARAMETERS\n"
        " 1 6 1.0E+00 0.0E+00 1.1E-02 0.0E+00 4.89630E+01-2.04710E+01 0 0 5.5E-03\n"
        "\n"
        " AVERAGE POWER GAIN: 9.97119E-01 - SOLID ANGLE USED IN AVERAGING: (+4.0000)*PI STERADIANS\n"
        "\n";
    const auto result = necwb::analysis::NecOutputParser{}.parse(output);
    expect(result.feedpoints.size() == 3, "NEC output parser reads each frequency block");
    expect(result.feedpoints[0].frequencyMHz == 14.0
            && result.feedpoints[0].wireTag == 1
            && result.feedpoints[0].segment == 6,
        "feedpoint results retain frequency and source location");
    expect(result.feedpoints[0].impedance == std::complex<double>{75.0, 15.0},
        "feedpoint results retain complex impedance");
    expect(result.feedpoints[1].frequencyMHz == 14.1
            && result.feedpoints[1].impedance == std::complex<double>{50.0, 0.0},
        "parser accepts Fortran D exponent output");
    expect(result.feedpoints[2].frequencyMHz == 14.3
            && result.feedpoints[2].impedance == std::complex<double>{48.963, -20.471},
        "parser accepts OpenNEC frequency and adjacent signed fields");

    const std::string multipleSourceOutput =
        " FREQUENCY : 1.4200E+01 MHz\n"
        " --------- ANTENNA INPUT PARAMETERS ---------\n"
        " 1 6 1.0E+00 0.0E+00 1.0E-02 0.0E+00 5.0E+01 0.0E+00 0 0 5.0E-03\n"
        " 2 6 1.0E+00 0.0E+00 8.0E-03 1.0E-03 6.0E+01 -7.5E+00 0 0 4.0E-03\n"
        "\n";
    const auto multipleSourceResult = necwb::analysis::NecOutputParser{}.parse(
        multipleSourceOutput);
    expect(multipleSourceResult.feedpoints.size() == 2
            && multipleSourceResult.feedpoints[0].wireTag == 1
            && multipleSourceResult.feedpoints[1].wireTag == 2
            && multipleSourceResult.feedpoints[1].frequencyMHz == 14.2
            && multipleSourceResult.feedpoints[1].impedance
                == std::complex<double>{60.0, -7.5},
        "NEC output parser retains multiple source results at one frequency");
    expect(std::abs(necwb::analysis::standingWaveRatio({50.0, 0.0}) - 1.0) < 1.0e-12,
        "matched 50-ohm impedance has one-to-one SWR");
    expect(necwb::analysis::standingWaveRatio({0.0, 0.0})
            == std::numeric_limits<double>::infinity(),
        "zero impedance reports infinite SWR");
    expect(result.currents.size() == 3
            && result.currents[0].wireTag == 1
            && result.currents[0].segment == 1
            && result.currents[0].magnitude == 0.010198,
        "NEC output parser reads segment current distribution rows");
    expect(result.currents[2].wireTag == 2 && result.currents[2].segment == 1
            && result.currents[2].magnitude == 0.0070711,
        "global NEC current segments map to local tag-relative segments");
    expect(result.currents[0].necSegment == 1 && result.currents[2].necSegment == 3,
        "NEC output parser keeps the absolute segment number");
    const auto splitTag = necwb::analysis::NecOutputParser{}.parse(
        " FREQUENCY : 1.4000E+01 MHz\n"
        " CURRENTS AND LOCATION\n"
        " 1 1 0 0 0 0.1 1.0E-02 0 1.0E-02 0\n"
        " 2 2 0 0 0 0.1 1.0E-02 0 1.0E-02 0\n"
        " 3 1 0 0 0 0.1 1.0E-02 0 1.0E-02 0\n"
        "\n");
    expect(splitTag.currents.size() == 3 && splitTag.currents[2].wireTag == 1
            && splitTag.currents[2].segment == 2 && splitTag.currents[2].necSegment == 3,
        "tag-relative segments count through wires that share a tag but are not adjacent");
    expect(result.radiation.size() == 5
            && result.radiation[1].thetaDegrees == 90.0
            && result.radiation[1].totalGainDb == 2.15
            && result.radiation[1].polarizationSense
                == necwb::analysis::PolarizationSense::Linear,
        "NEC output parser reads radiation gain samples");
    expect(result.radiation[3].frequencyMHz == 14.2
            && result.radiation[3].totalGainDb == 2.25
            && result.radiation[3].patternIndex == 0
            && result.radiation[4].patternIndex == 1,
        "NEC output parser retains frequency and RP dataset identity");
    expect(std::abs(necwb::analysis::radiationGainDb(result.radiation[1],
                necwb::analysis::RadiationComponent::RightHandCircular)
            - (2.15 - 3.0102999566)) < 1.0e-9,
        "linear polarization divides equally into circular components");
    expect(std::abs(necwb::analysis::radiationGainDb(result.radiation[2],
                necwb::analysis::RadiationComponent::RightHandCircular) + 7.85) < 1.0e-12
            && necwb::analysis::radiationGainDb(result.radiation[2],
                necwb::analysis::RadiationComponent::LeftHandCircular) < -900.0,
        "circular polarization follows the NEC axial ratio and sense");
    expect(result.averagePowerGain && result.averagingSolidAnglePi
            && std::abs(*result.averagePowerGain - 0.997119) < 1.0e-12
            && *result.averagingSolidAnglePi == 4.0,
        "NEC output parser reads average-gain-test values");
    const auto metrics = necwb::analysis::radiationMetrics(
        std::span<const necwb::analysis::RadiationSample>{result.radiation.data(), 3},
        necwb::analysis::RadiationComponent::Total);
    expect(metrics.valid && metrics.peakGainDb == 2.15
            && metrics.peakThetaDegrees == 90.0
            && metrics.peakPhiDegrees == 0.0
            && metrics.frontToBackDb
            && std::abs(*metrics.frontToBackDb - 10.0) < 1.0e-12,
        "radiation metrics report peak direction and front-to-back ratio");

    // Over ground an elevated lobe's antipode is below the horizon and never sampled; F/B
    // uses the same elevation turned 180 degrees in azimuth.
    const std::array elevatedPattern{
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 60.0,
            .phiDegrees = 0.0, .totalGainDb = 7.0},
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 60.0,
            .phiDegrees = 180.0, .totalGainDb = -5.0},
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 90.0,
            .phiDegrees = 180.0, .totalGainDb = -20.0},
    };
    const auto elevatedMetrics = necwb::analysis::radiationMetrics(elevatedPattern,
        necwb::analysis::RadiationComponent::Total);
    const auto elevatedSpecified = necwb::analysis::radiationFrequencyMetrics(elevatedPattern,
        necwb::analysis::RadiationComponent::Total, 60.0, 0.0);
    expect(elevatedMetrics.frontToBackDb
            && std::abs(*elevatedMetrics.frontToBackDb - 12.0) < 1.0e-12
            && elevatedSpecified.size() == 1 && elevatedSpecified.front().frontToBackDb
            && std::abs(*elevatedSpecified.front().frontToBackDb - 12.0) < 1.0e-12,
        "F/B of an elevated lobe uses the same elevation at the opposite azimuth");

    const std::vector<necwb::analysis::RadiationSample> frequencyPatterns{
        {.frequencyMHz = 14.0, .thetaDegrees = 55.0, .phiDegrees = 0.0,
            .totalGainDb = 6.2},
        {.frequencyMHz = 14.0, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .totalGainDb = -999.99},
        {.frequencyMHz = 14.3, .thetaDegrees = 60.0, .phiDegrees = 0.0,
            .totalGainDb = 6.3},
        {.frequencyMHz = 14.3, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .totalGainDb = -999.99},
    };
    const auto nearestPeak = necwb::analysis::radiationPeakAtNearestFrequency(
        frequencyPatterns, necwb::analysis::RadiationComponent::Total, 14.05);
    expect(nearestPeak && nearestPeak->frequencyMHz == 14.0
            && nearestPeak->sampleCount == 2
            && nearestPeak->metrics.peakThetaDegrees == 55.0
            && nearestPeak->metrics.peakPhiDegrees == 0.0,
        "radiation peak selection uses the pattern nearest the requested frequency");

    std::vector<necwb::analysis::RadiationCutPoint> elevationCut;
    for (auto theta = -90; theta <= 90; ++theta)
        elevationCut.push_back({static_cast<double>(theta), static_cast<double>(theta), 90.0, -20.0});
    const auto setElevationGain = [&elevationCut](int theta, double gain) {
        elevationCut[static_cast<std::size_t>(theta + 90)].gainDb = gain;
    };
    for (auto theta = -82; theta <= -69; ++theta) setElevationGain(theta, 6.5);
    setElevationGain(-83, 5.08);
    setElevationGain(-82, 5.96);
    setElevationGain(-76, 8.20);
    setElevationGain(-69, 5.88);
    setElevationGain(-68, 5.05);
    setElevationGain(76, 8.20);
    const auto elevationMetrics = necwb::analysis::radiationCutMetrics(elevationCut, false);
    expect(elevationMetrics.valid && elevationMetrics.peakAngleDegrees == -76.0
            && elevationMetrics.tiedPeakAnglesDegrees.size() == 2
            && elevationMetrics.tiedPeakAnglesDegrees[1] == 76.0
            && elevationMetrics.sampledBeamwidthDegrees
            && std::abs(*elevationMetrics.sampledBeamwidthDegrees - 13.0) < 1.0e-12
            && elevationMetrics.interpolatedBeamwidthDegrees
            && std::abs(*elevationMetrics.interpolatedBeamwidthDegrees - 14.6829135) < 1.0e-6
            && elevationMetrics.frontToBackDb && std::abs(*elevationMetrics.frontToBackDb) < 1.0e-12,
        "elevation-cut metrics retain tied peaks, interpolate HPBW, and take F/B at the same elevation");

    std::vector<necwb::analysis::RadiationCutPoint> horizontalCut;
    for (auto phi = 0; phi < 360; ++phi)
        horizontalCut.push_back({static_cast<double>(phi), 62.0,
            static_cast<double>(phi), phi == 0 ? 8.0 : -2.0});
    const auto offHorizonMetrics = necwb::analysis::radiationCutMetrics(horizontalCut, true);
    expect(offHorizonMetrics.valid && offHorizonMetrics.frontToBackDb
            && std::abs(*offHorizonMetrics.frontToBackDb - 10.0) < 1.0e-12
            && offHorizonMetrics.sampledBeamwidthDegrees
            && *offHorizonMetrics.sampledBeamwidthDegrees == 0.0
            && offHorizonMetrics.interpolatedBeamwidthDegrees
            && std::abs(*offHorizonMetrics.interpolatedBeamwidthDegrees - 0.6) < 1.0e-12,
        "off-horizon azimuth cut wraps HPBW across the seam and finds F/B in the cut");
    for (auto& sample : horizontalCut) sample.thetaDegrees = 90.0;
    const auto horizonMetrics = necwb::analysis::radiationCutMetrics(horizontalCut, true);
    expect(horizonMetrics.frontToBackDb
            && std::abs(*horizonMetrics.frontToBackDb - 10.0) < 1.0e-12,
        "horizon azimuth cut finds its physical antipodal sample");
}

void testOptimizationObjectives()
{
    const std::vector<necwb::analysis::FeedpointResult> feedpoints{
        {.frequencyMHz = 7.0, .impedance = {50.0, 0.0}},
        {.frequencyMHz = 7.1, .impedance = {75.0, 0.0}},
        {.frequencyMHz = 7.2, .impedance = {100.0, 0.0}},
    };
    const auto maximum = necwb::analysis::evaluateOptimizationObjective(feedpoints,
        {.kind = necwb::analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies,
            .referenceImpedance = 50.0});
    expect(maximum && maximum->feedpoint && maximum->feedpoint->frequencyMHz == 7.2
            && std::abs(maximum->score - 2.0) < 1.0e-12,
        "maximum-SWR objective scores the limiting sweep frequency");

    const auto selected = necwb::analysis::evaluateOptimizationObjective(feedpoints,
        {.kind = necwb::analysis::OptimizationObjectiveKind::SwrAtFrequency,
            .referenceImpedance = 50.0, .targetFrequencyMHz = 7.11});
    expect(selected && selected->feedpoint && selected->feedpoint->frequencyMHz == 7.1
            && selected->score < maximum->score,
        "selected-frequency objective scores the nearest calculated frequency");
    const auto average = necwb::analysis::evaluateOptimizationObjective(feedpoints,
        {.kind = necwb::analysis::OptimizationObjectiveKind::AverageAcrossFrequencies,
            .referenceImpedance = 50.0});
    expect(average && average->feedpoint
            && std::abs(average->score - 1.5) < 1.0e-12
            && std::abs(average->swr - 1.5) < 1.0e-12
            && std::abs(average->feedpoint->impedance.real() - 75.0) < 1.0e-12
            && average->swrSummary
            && std::abs(average->swrSummary->minimum - 1.0) < 1.0e-12
            && std::abs(average->swrSummary->average - 1.5) < 1.0e-12
            && std::abs(average->swrSummary->maximum - 2.0) < 1.0e-12,
        "average objective scores the mean across frequencies and reports SWR statistics");

    auto perCriterion = necwb::analysis::OptimizationObjectiveSpec{};
    perCriterion.swrAggregation = necwb::analysis::OptimizationAggregation::Maximum;
    const auto perCriterionMaximum = necwb::analysis::evaluateOptimizationObjective(
        feedpoints, perCriterion);
    expect(perCriterionMaximum && perCriterionMaximum->evaluatedFrequencyCount == 3
            && perCriterionMaximum->evaluationFrequencyMHz == 0.0
            && std::abs(perCriterionMaximum->score - 2.0) < 1.0e-12,
        "per-criterion SWR defaults to minimizing the maximum across frequencies");
    perCriterion.swrAggregation = necwb::analysis::OptimizationAggregation::Average;
    const auto perCriterionAverage = necwb::analysis::evaluateOptimizationObjective(
        feedpoints, perCriterion);
    expect(perCriterionAverage && std::abs(perCriterionAverage->score - 1.5) < 1.0e-12,
        "per-criterion SWR can minimize its average across frequencies");

    auto resistanceTarget = necwb::analysis::OptimizationObjectiveSpec{};
    resistanceTarget.swrWeight = 0.0;
    resistanceTarget.resistanceWeight = 1.0;
    resistanceTarget.resistanceTargetOhms = 60.0;
    const auto resistanceTargetResult = necwb::analysis::evaluateOptimizationObjective(
        feedpoints, resistanceTarget);
    expect(resistanceTargetResult && resistanceTargetResult->feedpoint
            && resistanceTargetResult->feedpoint->impedance.real() == 100.0
            && std::abs(resistanceTargetResult->score - 0.8) < 1.0e-12,
        "target criteria reduce the maximum absolute error across frequencies");

    auto goodEnough = necwb::analysis::OptimizationObjectiveSpec{};
    goodEnough.swrGoal = necwb::analysis::OptimizationGoal::GoodEnough;
    goodEnough.swrTarget = 2.1;
    const auto satisfiedThreshold = necwb::analysis::evaluateOptimizationObjective(
        feedpoints, goodEnough);
    expect(satisfiedThreshold && satisfiedThreshold->score == 0.0,
        "good-enough at-most criteria add no penalty when every point meets the threshold");
    goodEnough.swrTarget = 1.5;
    const auto violatedThreshold = necwb::analysis::evaluateOptimizationObjective(
        feedpoints, goodEnough);
    expect(violatedThreshold && std::abs(violatedThreshold->score - 0.5) < 1.0e-12,
        "good-enough at-most criteria score the maximum threshold violation");
    goodEnough.swrGoodEnoughDirection = necwb::analysis::GoodEnoughDirection::AtLeast;
    goodEnough.swrTarget = 1.8;
    const auto unusualThreshold = necwb::analysis::evaluateOptimizationObjective(
        feedpoints, goodEnough);
    expect(unusualThreshold && std::abs(unusualThreshold->score - 0.8) < 1.0e-12,
        "every criterion supports an explicit good-enough direction");

    const std::vector<necwb::analysis::FeedpointResult> weightedFeedpoint{
        {.frequencyMHz = 14.2, .impedance = {75.0, 25.0}},
    };
    const auto weighted = necwb::analysis::evaluateOptimizationObjective(weightedFeedpoint,
        {.kind = necwb::analysis::OptimizationObjectiveKind::SwrAtFrequency,
            .referenceImpedance = 50.0,
            .targetFrequencyMHz = 14.2,
            .swrWeight = 0.0,
            .resistanceWeight = 2.0,
            .resistanceTargetOhms = 50.0,
            .reactanceWeight = 1.0,
            .reactanceTargetOhms = 0.0});
    expect(weighted && std::abs(weighted->score - 0.5) < 1.0e-12
            && weighted->swrComponent == 0.0
            && std::abs(weighted->resistanceComponent - 1.0 / 3.0) < 1.0e-12
            && std::abs(weighted->reactanceComponent - 1.0 / 6.0) < 1.0e-12,
        "weighted impedance objective normalizes resistance and reactance errors");
    const auto disabled = necwb::analysis::evaluateOptimizationObjective(weightedFeedpoint,
        {.swrWeight = 0.0, .resistanceWeight = 0.0, .reactanceWeight = 0.0});
    expect(!disabled, "optimization objective requires at least one positive weight");

    necwb::analysis::AnalysisResult directional;
    directional.radiation = {
        {.frequencyMHz = 14.2, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .verticalGainDb = 7.0, .horizontalGainDb = -10.0, .totalGainDb = 7.2},
        {.frequencyMHz = 14.2, .thetaDegrees = 90.0, .phiDegrees = 180.0,
            .verticalGainDb = -13.0, .horizontalGainDb = -20.0, .totalGainDb = -12.8},
        {.frequencyMHz = 14.2, .thetaDegrees = 90.0, .phiDegrees = 90.0,
            .verticalGainDb = -2.0, .horizontalGainDb = -20.0, .totalGainDb = -2.0},
        {.frequencyMHz = 14.2, .thetaDegrees = 90.0, .phiDegrees = 270.0,
            .verticalGainDb = -5.0, .horizontalGainDb = -20.0, .totalGainDb = -5.0},
        {.frequencyMHz = 14.2, .thetaDegrees = 30.0, .phiDegrees = 45.0,
            .verticalGainDb = -40.0, .horizontalGainDb = -40.0, .totalGainDb = -40.0},
    };
    const necwb::analysis::OptimizationObjectiveSpec directionalObjective{
        .kind = necwb::analysis::OptimizationObjectiveKind::SwrAtFrequency,
        .targetFrequencyMHz = 14.2,
        .swrWeight = 0.0,
        .forwardGainWeight = 1.0,
        .frontToBackWeight = 1.0,
        .forwardThetaDegrees = 90.0,
        .forwardPhiDegrees = 0.0,
        .radiationComponent = necwb::analysis::RadiationComponent::Total,
    };
    const auto directionalScore = necwb::analysis::evaluateOptimizationObjective(
        directional, directionalObjective);
    expect(directionalScore && directionalScore->forwardGainDb
            && std::abs(*directionalScore->forwardGainDb - 7.2) < 1.0e-12
            && directionalScore->frontToBackDb
            && std::abs(*directionalScore->frontToBackDb - 20.0) < 1.0e-12
            && std::abs(directionalScore->score + 1.36) < 1.0e-12,
        "directional objective maximizes explicit forward gain and physical antipodal F/B");
    auto frontToRearObjective = directionalObjective;
    frontToRearObjective.forwardGainWeight = 0.0;
    frontToRearObjective.frontToBackWeight = 0.0;
    frontToRearObjective.frontToRearWeight = 1.0;
    const auto frontToRearScore = necwb::analysis::evaluateOptimizationObjective(
        directional, frontToRearObjective);
    expect(frontToRearScore && frontToRearScore->rearGainDb
            && std::abs(*frontToRearScore->rearGainDb - (-2.0)) < 1.0e-12
            && frontToRearScore->frontToRearDb
            && std::abs(*frontToRearScore->frontToRearDb - 9.2) < 1.0e-12,
        "F/R objective uses the strongest response in the rear azimuth half");
    directional.radiation.erase(directional.radiation.begin() + 1);
    expect(!necwb::analysis::evaluateOptimizationObjective(
            directional, directionalObjective),
        "directional F/B is unavailable when the physical opposite sample is absent");
    const auto missingBack = necwb::analysis::diagnoseOptimizationObjectiveFailure(
        directional, directionalObjective);
    expect(missingBack.kind
                == necwb::analysis::OptimizationObjectiveFailureKind::MissingBackDirection
            && missingBack.frequencyMHz
            && std::abs(*missingBack.frequencyMHz - 14.2) < 1.0e-12,
        "objective diagnostics identify the frequency with missing antipodal F/B coverage");
    directional.radiation.front().frequencyMHz = 14.1;
    auto forwardOnlyObjective = directionalObjective;
    forwardOnlyObjective.frontToBackWeight = 0.0;
    expect(!necwb::analysis::evaluateOptimizationObjective(
            directional, forwardOnlyObjective),
        "directional gain is unavailable when the requested frequency is absent");

    necwb::analysis::AnalysisResult nullPattern;
    nullPattern.feedpoints = {
        {.frequencyMHz = 14.2, .impedance = {75.0, 15.0}},
    };
    nullPattern.radiation = {
        {.frequencyMHz = 14.2, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .verticalGainDb = -999.99, .horizontalGainDb = -999.99,
            .totalGainDb = -999.99},
    };
    auto mixedNullObjective = forwardOnlyObjective;
    mixedNullObjective.swrWeight = 1.0;
    const auto nullFailure = necwb::analysis::diagnoseOptimizationObjectiveFailure(
        nullPattern, mixedNullObjective);
    expect(nullFailure.kind
                == necwb::analysis::OptimizationObjectiveFailureKind::NullForwardGain
            && nullFailure.frequencyMHz
            && std::abs(*nullFailure.frequencyMHz - 14.2) < 1.0e-12,
        "objective diagnostics distinguish an NEC pattern null from missing coverage");
    const auto nullMetrics = necwb::analysis::radiationFrequencyMetrics(
        nullPattern.radiation, necwb::analysis::RadiationComponent::Total, 90.0, 0.0);
    expect(nullMetrics.size() == 1
            && nullMetrics.front().forwardAvailability
                == necwb::analysis::RadiationMetricAvailability::PatternNull
            && !nullMetrics.front().forwardGainDb,
        "radiation metrics retain pattern-null availability without exposing invalid gain");

    const auto directionalDeck = necwb::analysis::prepareDirectionalOptimizationInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nFR 0 3 0 0 14 .1\n"
        "RP 0 19 37 1000 0 0 5 10\nEN\n",
        14.2, 90.0, 0.0, true);
    expect(directionalDeck.find("FR 0 1 0 0 14.2 0") != std::string::npos
            && directionalDeck.find("FR 0 1 0 0 14.2 0\nXQ 0\nRP")
                != std::string::npos
            && directionalDeck.find("RP 0 1 1 1000 90 0") != std::string::npos
            && directionalDeck.find("RP 0 1 1 1000 90 180") != std::string::npos
            && directionalDeck.find("RP 0 19 37") == std::string::npos,
        "directional optimization deck replaces broad requests with exact front/back samples");
    const auto frontToRearDeck = necwb::analysis::prepareDirectionalOptimizationInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nFR 0 1 0 0 14.2 0\nEN\n",
        14.2, 90.0, 0.0, false, true);
    expect(frontToRearDeck.find("RP 0 1 1 1000 90 0") != std::string::npos
            && frontToRearDeck.find("RP 0 1 1 1000 90 90") != std::string::npos
            && frontToRearDeck.find("RP 0 1 1 1000 90 180") != std::string::npos
            && frontToRearDeck.find("RP 0 1 1 1000 90 270") != std::string::npos,
        "F/R optimization deck samples the complete rear azimuth half");

    necwb::analysis::AnalysisResult directionalSweep;
    directionalSweep.feedpoints = {
        {.frequencyMHz = 144.0, .impedance = {50.0, 0.0}},
        {.frequencyMHz = 148.0, .impedance = {100.0, 0.0}},
    };
    directionalSweep.radiation = {
        {.frequencyMHz = 144.0, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .verticalGainDb = 8.0, .horizontalGainDb = -20.0, .totalGainDb = 8.0},
        {.frequencyMHz = 144.0, .thetaDegrees = 90.0, .phiDegrees = 180.0,
            .verticalGainDb = -12.0, .horizontalGainDb = -30.0, .totalGainDb = -12.0},
        {.frequencyMHz = 148.0, .thetaDegrees = 90.0, .phiDegrees = 0.0,
            .verticalGainDb = 5.0, .horizontalGainDb = -20.0, .totalGainDb = 5.0},
        {.frequencyMHz = 148.0, .thetaDegrees = 90.0, .phiDegrees = 180.0,
            .verticalGainDb = -5.0, .horizontalGainDb = -30.0, .totalGainDb = -5.0},
        {.frequencyMHz = 144.0, .thetaDegrees = 90.0, .phiDegrees = 90.0,
            .verticalGainDb = 0.0, .horizontalGainDb = -30.0, .totalGainDb = 0.0},
        {.frequencyMHz = 148.0, .thetaDegrees = 90.0, .phiDegrees = 90.0,
            .verticalGainDb = 1.0, .horizontalGainDb = -30.0, .totalGainDb = 1.0},
        {.frequencyMHz = 148.0, .thetaDegrees = 90.0, .phiDegrees = 45.0,
            .verticalGainDb = 7.0, .horizontalGainDb = -30.0, .totalGainDb = 7.0},
        {.frequencyMHz = 148.0, .thetaDegrees = 90.0, .phiDegrees = 225.0,
            .verticalGainDb = -3.0, .horizontalGainDb = -30.0, .totalGainDb = -3.0},
    };
    const auto directionalFrequencyMetrics = necwb::analysis::radiationFrequencyMetrics(
        directionalSweep.radiation, necwb::analysis::RadiationComponent::Total, 90.0, 0.0);
    expect(directionalFrequencyMetrics.size() == 2
            && directionalFrequencyMetrics.front().frequencyMHz == 144.0
            && directionalFrequencyMetrics.front().forwardGainDb
            && *directionalFrequencyMetrics.front().forwardGainDb == 8.0
            && directionalFrequencyMetrics.front().frontToBackDb
            && *directionalFrequencyMetrics.front().frontToBackDb == 20.0
            && directionalFrequencyMetrics.back().frontToRearDb
            && *directionalFrequencyMetrics.back().frontToRearDb == 4.0,
        "shared radiation frequency metrics retain gain, F/B, and F/R");

    const auto trackedCutMetrics = necwb::analysis::radiationTrackedCutFrequencyMetrics(
        directionalSweep.radiation, necwb::analysis::RadiationComponent::Total,
        necwb::analysis::DirectionalEvaluationMode::StrongestInAzimuthCut, 90.0);
    expect(trackedCutMetrics.size() == 2
            && trackedCutMetrics.front().forwardGainDb
            && *trackedCutMetrics.front().forwardGainDb == 8.0
            && trackedCutMetrics.front().forwardPhiDegrees
            && *trackedCutMetrics.front().forwardPhiDegrees == 0.0
            && trackedCutMetrics.back().forwardGainDb
            && *trackedCutMetrics.back().forwardGainDb == 7.0
            && trackedCutMetrics.back().forwardPhiDegrees
            && *trackedCutMetrics.back().forwardPhiDegrees == 45.0
            && trackedCutMetrics.back().frontToBackDb
            && *trackedCutMetrics.back().frontToBackDb == 10.0,
        "tracked azimuth-cut metrics select the strongest sampled lobe per frequency");
    const std::array elevatedAzimuthCut{
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 55.0,
            .phiDegrees = 0.0, .totalGainDb = 6.0},
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 55.0,
            .phiDegrees = 180.0, .totalGainDb = -9.0},
    };
    const auto elevatedCutMetrics = necwb::analysis::radiationTrackedCutFrequencyMetrics(
        elevatedAzimuthCut, necwb::analysis::RadiationComponent::Total,
        necwb::analysis::DirectionalEvaluationMode::StrongestInAzimuthCut, 55.0);
    expect(elevatedCutMetrics.size() == 1
            && elevatedCutMetrics.front().frontToBackDb
            && *elevatedCutMetrics.front().frontToBackDb == 15.0,
        "tracked azimuth-cut F/B uses the opposite azimuth at the same theta");
    // A stronger lobe in another vertical plane must not be taken as the elevation-cut peak.
    const std::array twoElevationCuts{
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 60.0,
            .phiDegrees = 30.0, .totalGainDb = 5.0},
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = -60.0,
            .phiDegrees = 30.0, .totalGainDb = 3.0},
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 60.0,
            .phiDegrees = 120.0, .totalGainDb = 9.0, .patternIndex = 1},
    };
    const auto elevationCutMetrics = necwb::analysis::radiationTrackedCutFrequencyMetrics(
        twoElevationCuts, necwb::analysis::RadiationComponent::Total,
        necwb::analysis::DirectionalEvaluationMode::StrongestInElevationCut, 30.0);
    expect(elevationCutMetrics.size() == 1 && elevationCutMetrics.front().forwardGainDb
            && *elevationCutMetrics.front().forwardGainDb == 5.0
            && elevationCutMetrics.front().forwardPhiDegrees
            && *elevationCutMetrics.front().forwardPhiDegrees == 30.0,
        "tracked elevation-cut metrics consider only the requested vertical plane");
    // The elevation cut spans theta -90..90 at one phi; negative theta is the opposite azimuth,
    // so an elevated lobe's back sample is in the same cut.
    const std::array elevatedElevationCut{
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 45.0,
            .phiDegrees = 30.0, .totalGainDb = 6.0},
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = -45.0,
            .phiDegrees = 30.0, .totalGainDb = -4.0},
        necwb::analysis::RadiationSample{.frequencyMHz = 14.2, .thetaDegrees = 0.0,
            .phiDegrees = 30.0, .totalGainDb = 1.0},
    };
    const auto elevatedElevationMetrics = necwb::analysis::radiationTrackedCutFrequencyMetrics(
        elevatedElevationCut, necwb::analysis::RadiationComponent::Total,
        necwb::analysis::DirectionalEvaluationMode::StrongestInElevationCut, 30.0);
    expect(elevatedElevationMetrics.size() == 1
            && elevatedElevationMetrics.front().frontToBackDb
            && std::abs(*elevatedElevationMetrics.front().frontToBackDb - 10.0) < 1.0e-9,
        "tracked elevation-cut F/B uses the opposite azimuth at the same elevation");
    auto trackedObjective = directionalObjective;
    trackedObjective.kind = necwb::analysis::OptimizationObjectiveKind::PerCriterion;
    trackedObjective.directionalEvaluationMode =
        necwb::analysis::DirectionalEvaluationMode::StrongestInAzimuthCut;
    const auto trackedScore = necwb::analysis::evaluateOptimizationObjective(
        directionalSweep, trackedObjective);
    expect(trackedScore && trackedScore->frequencyMetrics.size() == 2
            && trackedScore->frequencyMetrics.back().forwardGainDb
            && *trackedScore->frequencyMetrics.back().forwardGainDb == 7.0
            && trackedScore->frequencyMetrics.back().forwardThetaDegrees
            && *trackedScore->frequencyMetrics.back().forwardThetaDegrees == 90.0
            && trackedScore->frequencyMetrics.back().forwardPhiDegrees
            && *trackedScore->frequencyMetrics.back().forwardPhiDegrees == 45.0,
        "optimization objectives retain the direction selected by tracked cuts");
    auto directionalMinimax = directionalObjective;
    directionalMinimax.kind = necwb::analysis::OptimizationObjectiveKind::WorstPointAcrossFrequencies;
    directionalMinimax.swrWeight = 1.0;
    const auto sweepScore = necwb::analysis::evaluateOptimizationObjective(
        directionalSweep, directionalMinimax);
    expect(sweepScore && sweepScore->evaluationFrequencyMHz == 148.0
            && sweepScore->feedpoint && sweepScore->feedpoint->frequencyMHz == 148.0
            && sweepScore->forwardGainDb && *sweepScore->forwardGainDb == 5.0
            && sweepScore->forwardGainFrequencyMHz
            && *sweepScore->forwardGainFrequencyMHz == 148.0
            && sweepScore->frontToBackDb && *sweepScore->frontToBackDb == 10.0
            && sweepScore->frontToBackFrequencyMHz
            && *sweepScore->frontToBackFrequencyMHz == 148.0
            && sweepScore->frequencyMetrics.size() == 2
            && sweepScore->frequencyMetrics.front().frequencyMHz == 144.0
            && sweepScore->frequencyMetrics.front().forwardGainDb
            && *sweepScore->frequencyMetrics.front().forwardGainDb == 8.0
            && sweepScore->frequencyMetrics.front().frontToBackDb
            && *sweepScore->frequencyMetrics.front().frontToBackDb == 20.0
            && sweepScore->forwardGainSummary
            && sweepScore->forwardGainSummary->minimumFrequencyMHz == 148.0
            && sweepScore->forwardGainSummary->maximumFrequencyMHz == 144.0
            && std::abs(sweepScore->score - 1.0 / 6.0) < 1.0e-12,
        "directional minimax retains raw per-frequency metrics and extrema frequencies");
    auto frontToRearMinimax = directionalMinimax;
    frontToRearMinimax.swrWeight = 0.0;
    frontToRearMinimax.forwardGainWeight = 0.0;
    frontToRearMinimax.frontToBackWeight = 0.0;
    frontToRearMinimax.frontToRearWeight = 1.0;
    const auto frontToRearSweepScore = necwb::analysis::evaluateOptimizationObjective(
        directionalSweep, frontToRearMinimax);
    expect(frontToRearSweepScore && frontToRearSweepScore->frontToRearDb
            && *frontToRearSweepScore->frontToRearDb == 4.0
            && frontToRearSweepScore->frontToRearFrequencyMHz
            && *frontToRearSweepScore->frontToRearFrequencyMHz == 148.0,
        "directional minimax retains the lowest F/R across frequencies");
    auto directionalAverage = frontToRearMinimax;
    directionalAverage.kind =
        necwb::analysis::OptimizationObjectiveKind::AverageAcrossFrequencies;
    const auto averageDirectionalScore = necwb::analysis::evaluateOptimizationObjective(
        directionalSweep, directionalAverage);
    expect(averageDirectionalScore && averageDirectionalScore->frontToRearDb
            && *averageDirectionalScore->frontToRearDb == 6.0
            && std::abs(averageDirectionalScore->score - (-0.6)) < 1.0e-12
            && averageDirectionalScore->frontToRearSummary
            && averageDirectionalScore->frontToRearSummary->minimum == 4.0
            && averageDirectionalScore->frontToRearSummary->average == 6.0
            && averageDirectionalScore->frontToRearSummary->maximum == 8.0,
        "directional average objective reports mean F/R and range statistics");

    auto perCriterionGain = necwb::analysis::OptimizationObjectiveSpec{};
    perCriterionGain.swrWeight = 0.0;
    perCriterionGain.forwardGainWeight = 1.0;
    perCriterionGain.forwardGainAggregation =
        necwb::analysis::OptimizationAggregation::Minimum;
    const auto minimumGainScore = necwb::analysis::evaluateOptimizationObjective(
        directionalSweep, perCriterionGain);
    expect(minimumGainScore && minimumGainScore->forwardGainDb
            && *minimumGainScore->forwardGainDb == 5.0
            && std::abs(minimumGainScore->score - (-0.5)) < 1.0e-12,
        "per-criterion gain defaults to maximizing the minimum across frequencies");
    perCriterionGain.forwardGainGoal = necwb::analysis::OptimizationGoal::Minimize;
    perCriterionGain.forwardGainAggregation =
        necwb::analysis::OptimizationAggregation::Maximum;
    const auto unusualGainScore = necwb::analysis::evaluateOptimizationObjective(
        directionalSweep, perCriterionGain);
    expect(unusualGainScore && unusualGainScore->forwardGainDb
            && *unusualGainScore->forwardGainDb == 8.0
            && std::abs(unusualGainScore->score - 0.8) < 1.0e-12,
        "gain can use non-default goals without a separate optimizer algorithm");

    const std::array directionalFrequencies{144.0, 148.0};
    const auto directionalSweepDeck = necwb::analysis::prepareDirectionalOptimizationInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nFR 0 1 0 0 146 0\nEN\n",
        directionalFrequencies, 90.0, 0.0, true);
    expect(directionalSweepDeck.find("FR 0 1 0 0 144 0") != std::string::npos
            && directionalSweepDeck.find("FR 0 1 0 0 148 0") != std::string::npos
            && directionalSweepDeck.find("FR 0 1 0 0 144 0\nXQ 0\nRP")
                != std::string::npos
            && directionalSweepDeck.find("FR 0 1 0 0 148 0\nXQ 0\nRP")
                != std::string::npos
            && std::count(directionalSweepDeck.begin(), directionalSweepDeck.end(), '\n') >= 9,
        "directional optimization deck executes exact front/back requests at every study frequency");

    const auto azimuthCutDeck = necwb::analysis::prepareDirectionalCutOptimizationInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nFR 0 1 0 0 146 0\nEN\n",
        directionalFrequencies,
        necwb::analysis::DirectionalEvaluationMode::StrongestInAzimuthCut,
        55.0, true, true);
    expect(azimuthCutDeck.find("RP 0 1 72 1000 55 0 0 5") != std::string::npos
            && azimuthCutDeck.find("RP 0 1 72 1000 125 0 0 5") == std::string::npos
            && std::count(azimuthCutDeck.begin(), azimuthCutDeck.end(), '\n') >= 7,
        "azimuth tracking uses one complete fixed-theta cut at every frequency");
    const auto azimuthRequirements =
        necwb::analysis::optimizationRadiationRequirements(azimuthCutDeck);
    expect(azimuthRequirements.size() == 2
            && azimuthRequirements[0].frequencyMHz == 144.0
            && azimuthRequirements[1].frequencyMHz == 148.0
            && azimuthRequirements[0].patternIndex == 0
            && azimuthRequirements[1].patternIndex == 0
            && azimuthRequirements[0].sampleCount == 72
            && azimuthRequirements[0].directionFingerprint
                == azimuthRequirements[1].directionFingerprint,
        "optimization radiation requirements preserve each generated azimuth cut grid");
    const auto elevationCutDeck = necwb::analysis::prepareDirectionalCutOptimizationInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nEN\n",
        directionalFrequencies,
        necwb::analysis::DirectionalEvaluationMode::StrongestInElevationCut,
        30.0, true, true);
    expect(elevationCutDeck.find("RP 0 37 1 1000 -90 30 5 0") != std::string::npos,
        "elevation tracking requests a complete fixed-phi cut");
    const auto elevationRequirements =
        necwb::analysis::optimizationRadiationRequirements(elevationCutDeck);
    expect(elevationRequirements.size() == 2
            && elevationRequirements[0].sampleCount == 37
            && elevationRequirements[0].directionFingerprint
                == elevationRequirements[1].directionFingerprint
            && elevationRequirements[0].directionFingerprint
                != azimuthRequirements[0].directionFingerprint,
        "optimization radiation requirements distinguish elevation and azimuth grids");

    const auto elevatedBackDeck = necwb::analysis::prepareDirectionalOptimizationInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nEN\n",
        directionalFrequencies, 60.0, 30.0, true, false);
    expect(elevatedBackDeck.find("RP 0 1 1 1000 60 210") != std::string::npos
            && elevatedBackDeck.find("RP 0 1 1 1000 120") == std::string::npos,
        "specified-direction F/B requests the same elevation at the opposite azimuth");

    const auto specifiedRearDeck = necwb::analysis::prepareDirectionalOptimizationInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nEN\n",
        directionalFrequencies, 90.0, 0.0, true, true);
    const auto specifiedRearRequirements =
        necwb::analysis::optimizationRadiationRequirements(specifiedRearDeck);
    expect(specifiedRearRequirements.size() == 76
            && specifiedRearRequirements[0].frequencyMHz == 144.0
            && specifiedRearRequirements[0].patternIndex == 0
            && specifiedRearRequirements[37].patternIndex == 37
            && specifiedRearRequirements[38].frequencyMHz == 148.0
            && specifiedRearRequirements[38].patternIndex == 0
            && std::ranges::all_of(specifiedRearRequirements,
                [](const auto& requirement) { return requirement.sampleCount == 1; }),
        "specified-direction F/R requirements retain every rear-region request per frequency");
    const auto changedGeometryRearDeck =
        necwb::analysis::prepareDirectionalOptimizationInput(
            "GW 1 3 0 0 0 2 0 0 .001\nGE 0\nEN\n",
            directionalFrequencies, 90.0, 0.0, true, true);
    expect(necwb::analysis::optimizationRadiationRequirements(changedGeometryRearDeck)
            == specifiedRearRequirements,
        "candidate geometry changes do not alter the required directional sample grid");

    const auto maximumGainDeck = necwb::analysis::prepareMaximumGainSearchInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nFR 0 3 0 0 14 .1\n"
        "XQ 0\nRP 0 1 1 1000 90 0 0 0\nEN\n",
        14.2, true);
    const auto maximumGainSetup = necwb::nec::NecSetupConverter{}.convert(
        necwb::nec::NecParser{}.parse(maximumGainDeck));
    expect(maximumGainSetup.frequency && maximumGainSetup.frequency->count == 1
            && std::abs(maximumGainSetup.frequency->startMHz - 14.2) < 1.0e-12
            && maximumGainSetup.executionRequest.has_value()
            && maximumGainSetup.radiationPatterns.size() == 1
            && maximumGainSetup.radiationPatterns.front().thetaCount == 19
            && maximumGainSetup.radiationPatterns.front().phiCount == 72
            && maximumGainSetup.radiationPatterns.front().thetaStep == 5.0
            && maximumGainSetup.radiationPatterns.front().phiStep == 5.0,
        "maximum-gain search replaces authored requests with a one-frequency hemisphere grid");
    const auto freeSpaceMaximumGainDeck = necwb::analysis::prepareMaximumGainSearchInput(
        "GW 1 3 0 0 0 1 0 0 .001\nGE 0\nEN\n", 28.5, false);
    const auto freeSpaceMaximumGainSetup = necwb::nec::NecSetupConverter{}.convert(
        necwb::nec::NecParser{}.parse(freeSpaceMaximumGainDeck));
    expect(freeSpaceMaximumGainSetup.radiationPatterns.size() == 1
            && freeSpaceMaximumGainSetup.radiationPatterns.front().thetaCount == 37
            && freeSpaceMaximumGainSetup.radiationPatterns.front().phiCount == 72,
        "maximum-gain search covers the full sphere for a free-space model");
}

void testAdaptiveSearch()
{
    necwb::analysis::AdaptiveSearch search({0.0, 10.0, 12, 0.1, 0.001});
    const auto initial = search.initialCandidates();
    expect(initial == std::vector<double>({0.0, 2.5, 5.0, 7.5, 10.0}),
        "adaptive search begins with a bounded five-point sample");
    for (const auto value : initial) search.record(value, std::pow(value - 5.0, 2.0));
    const auto firstRefinement = search.nextCandidates();
    expect(firstRefinement == std::vector<double>({3.75, 6.25}),
        "adaptive search refines both neighbors of an interior best point");
    expect(search.refinementRound() == 1,
        "adaptive search numbers a left/right proposal batch as one refinement round");
    for (const auto value : firstRefinement) search.record(value, std::pow(value - 5.0, 2.0));
    const auto secondRefinement = search.nextCandidates();
    expect(search.refinementRound() == 2,
        "adaptive search advances its round only after proposing the next batch");
    for (const auto value : secondRefinement) search.record(value, std::pow(value - 5.0, 2.0));
    expect(search.nextCandidates().empty()
            && search.stopReason() == necwb::analysis::AdaptiveStopReason::ScoreTolerance,
        "adaptive search reports score-tolerance convergence after repeated stagnant rounds");
    expect(search.scoreImprovements() == std::vector<double>({0.0, 0.0}),
        "adaptive search retains the round improvements that triggered convergence");

    necwb::analysis::AdaptiveSearch limited({0.0, 10.0, 5, 0.1, 0.001});
    for (const auto value : limited.initialCandidates()) limited.record(value, value);
    expect(limited.nextCandidates().empty()
            && limited.stopReason() == necwb::analysis::AdaptiveStopReason::MaximumEvaluations,
        "adaptive search obeys its solver-evaluation budget");

    necwb::analysis::AdaptiveSearch resolved({0.0, 1.0, 12, 0.2, 0.001});
    for (const auto value : resolved.initialCandidates())
        resolved.record(value, std::pow(value - 0.5, 2.0));
    expect(resolved.nextCandidates().empty()
            && resolved.stopReason() == necwb::analysis::AdaptiveStopReason::ParameterTolerance,
        "adaptive search reports parameter tolerance when neither midpoint is far enough away");

    necwb::analysis::AdaptiveSearch failed({0.0, 10.0, 12, 0.1, 0.001});
    for (const auto value : failed.initialCandidates()) failed.record(value, std::nullopt);
    expect(failed.nextCandidates().empty()
            && failed.stopReason() == necwb::analysis::AdaptiveStopReason::NoSuccessfulCandidate,
        "adaptive search reports when no candidate produced an objective score");

    necwb::analysis::AdaptiveVectorSearch vectorSearch({
        {{0.0, 10.0, 0.1}, {100.0, 200.0, 1.0}}, 15, 0.001});
    const auto initialVectors = vectorSearch.initialCandidates();
    expect(initialVectors.size() == 5
            && initialVectors.front().values == std::vector<double>({5.0, 150.0})
            && initialVectors[1].values == std::vector<double>({0.0, 150.0})
            && initialVectors[4].values == std::vector<double>({5.0, 200.0}),
        "multi-variable adaptive search samples the center and each variable boundary");
    for (const auto& candidate : initialVectors) {
        const auto score = std::pow(candidate.values[0] - 4.0, 2.0)
            + std::pow((candidate.values[1] - 140.0) / 10.0, 2.0);
        vectorSearch.record(candidate.values, score);
    }
    const auto refinedVectors = vectorSearch.nextCandidates();
    expect(!refinedVectors.empty()
            && std::ranges::all_of(refinedVectors, [](const auto& candidate) {
                return candidate.values.size() == 2;
            }),
        "multi-variable adaptive search proposes bounded coordinate refinements");

    necwb::analysis::AdaptiveVectorSearch boundarySearch({
        {{13.537, 20.305, 0.01}}, 21, 0.001});
    const auto boundaryInitial = boundarySearch.initialCandidates();
    boundarySearch.record(boundaryInitial[0].values, 25.0);
    boundarySearch.record(boundaryInitial[1].values, 30.0);
    boundarySearch.record(boundaryInitial[2].values, 18.983);
    const auto boundaryRoundOne = boundarySearch.nextCandidates();
    expect(boundaryRoundOne.size() == 1,
        "adaptive boundary refinement tests the remaining interior direction");
    boundarySearch.record(boundaryRoundOne.front().values, 20.155);
    const auto boundaryRoundTwo = boundarySearch.nextCandidates();
    expect(boundaryRoundTwo.size() == 1,
        "adaptive boundary refinement requires two completed stagnant rounds");
    boundarySearch.record(boundaryRoundTwo.front().values, 19.500);
    expect(boundarySearch.nextCandidates().empty()
            && boundarySearch.stopReason()
                == necwb::analysis::AdaptiveStopReason::ScoreTolerance
            && boundarySearch.scoreImprovements() == std::vector<double>({0.0, 0.0}),
        "adaptive boundary convergence reports unchanged best-score improvements");
}

void testNelderMeadSearch()
{
    necwb::analysis::NelderMeadSearch search({
        {{.initial = 8.0, .minimum = 0.0, .maximum = 10.0, .tolerance = 0.001},
            {.initial = 2.0, .minimum = -5.0, .maximum = 5.0, .tolerance = 0.001}},
        100,
        1.0e-8,
    });
    auto bestScore = std::numeric_limits<double>::infinity();
    const auto evaluate = [&search, &bestScore](const auto& proposal) {
        const auto score = std::pow(proposal.values[0] - 3.0, 2.0)
            + std::pow(proposal.values[1] + 1.0, 2.0);
        bestScore = std::min(bestScore, score);
        search.record(proposal.values, score);
    };
    const auto initial = search.initialCandidates();
    expect(initial.size() == 3 && initial.front().values == std::vector<double>({8.0, 2.0}),
        "Nelder-Mead creates one bounded simplex vertex per variable plus the initial point");
    for (const auto& proposal : initial) evaluate(proposal);
    while (const auto proposal = search.nextCandidate()) evaluate(*proposal);
    expect(bestScore < 1.0e-3,
        "Nelder-Mead converges near the minimum of a bounded two-variable objective");
    expect(search.evaluationCount() <= 100 && search.iteration() > 0,
        "Nelder-Mead tracks iterations and obeys its evaluation budget");
    expect(search.stopReason() != necwb::analysis::NelderMeadStopReason::None,
        "Nelder-Mead reports an explicit stopping reason");

    necwb::analysis::NelderMeadSearch failed({
        {{.initial = 0.5, .minimum = 0.0, .maximum = 1.0, .tolerance = 0.01}}, 8, 0.001});
    for (const auto& proposal : failed.initialCandidates())
        failed.record(proposal.values, std::nullopt);
    expect(!failed.nextCandidate()
            && failed.stopReason() == necwb::analysis::NelderMeadStopReason::NoSuccessfulCandidate,
        "Nelder-Mead stops when its initial simplex has no successful candidates");
}

void testDifferentialEvolutionSearch()
{
    const necwb::analysis::DifferentialEvolutionSettings settings{
        {{.initial = 4.0, .minimum = -5.0, .maximum = 5.0, .tolerance = 0.0001},
            {.initial = 4.0, .minimum = -5.0, .maximum = 5.0, .tolerance = 0.0001}},
        12,
        40,
        0.8,
        0.9,
        0.0,
        100,
        12345u,
    };
    necwb::analysis::DifferentialEvolutionSearch first(settings);
    necwb::analysis::DifferentialEvolutionSearch second(settings);
    const auto objective = [](const std::vector<double>& values) {
        return std::pow(values[0] - 1.5, 2.0) + std::pow(values[1] + 2.0, 2.0);
    };
    auto bestScore = std::numeric_limits<double>::infinity();
    const auto recordPair = [&](const auto& firstProposal, const auto& secondProposal) {
        expect(firstProposal.values == secondProposal.values,
            "Differential evolution reproduces candidate proposals for a fixed seed");
        for (auto index = std::size_t{}; index < firstProposal.values.size(); ++index) {
            expect(firstProposal.values[index] >= settings.variables[index].minimum
                    && firstProposal.values[index] <= settings.variables[index].maximum,
                "Differential evolution keeps every proposal within variable bounds");
        }
        const auto score = objective(firstProposal.values);
        bestScore = std::min(bestScore, score);
        first.record(firstProposal.values, score);
        second.record(secondProposal.values, score);
    };
    const auto firstInitial = first.initialCandidates();
    const auto secondInitial = second.initialCandidates();
    expect(firstInitial.size() == 12 && secondInitial.size() == firstInitial.size()
            && firstInitial.front().values == std::vector<double>({4.0, 4.0}),
        "Differential evolution creates a bounded seeded population including the current model");
    for (auto index = std::size_t{}; index < firstInitial.size(); ++index)
        recordPair(firstInitial[index], secondInitial[index]);
    while (const auto firstProposal = first.nextCandidate()) {
        const auto secondProposal = second.nextCandidate();
        expect(secondProposal.has_value(),
            "Differential evolution seeded runs have matching lengths");
        if (!secondProposal) break;
        recordPair(*firstProposal, *secondProposal);
    }
    expect(!second.nextCandidate(),
        "Differential evolution seeded runs stop together");
    expect(bestScore < 0.1,
        "Differential evolution approaches a bounded two-variable objective minimum");
    expect(first.evaluationCount() <= settings.populationSize
            * (settings.maximumGenerations + 1)
            && first.generation() > 0,
        "Differential evolution tracks generations and obeys its evaluation budget");
    expect(first.stopReason() != necwb::analysis::DifferentialEvolutionStopReason::None,
        "Differential evolution reports an explicit stopping reason");

    necwb::analysis::DifferentialEvolutionSearch failed({
        {{.initial = 0.5, .minimum = 0.0, .maximum = 1.0, .tolerance = 0.01}},
        4, 5, 0.8, 0.9, 0.001, 3, 7u});
    for (const auto& proposal : failed.initialCandidates())
        failed.record(proposal.values, std::nullopt);
    expect(!failed.nextCandidate()
            && failed.stopReason()
                == necwb::analysis::DifferentialEvolutionStopReason::NoSuccessfulCandidate,
        "Differential evolution stops when its initial population has no successful candidates");
}

void testFrequencyPlans()
{
    const necwb::analysis::FrequencyPlan plan{
        .mode = necwb::analysis::FrequencyPlanMode::Explicit,
        .pointsMHz = {21.2, 7.15, 7.15, -1.0},
        .ranges = {{14.0, 14.1, 0.05}, {10.15, 10.1, 0.01}},
    };
    const auto points = necwb::analysis::frequencyPlanPoints(plan);
    expect(points.size() == 5 && points[0] == 7.15 && points[1] == 14.0
            && std::abs(points[2] - 14.05) < 1.0e-12
            && points[3] == 14.1 && points[4] == 21.2,
        "frequency plans combine, sort, and deduplicate points and valid ranges");

    const auto& presets = necwb::analysis::amateurBandPresets();
    const auto fortyMeters = std::ranges::find(presets, std::string_view{"40 m"},
        &necwb::analysis::AmateurBandPreset::name);
    expect(fortyMeters != presets.end() && fortyMeters->startMHz == 7.0
            && fortyMeters->endMHz == 7.3 && fortyMeters->stepMHz > 0.0,
        "amateur-band presets provide an editable 40-meter evaluation range");
}

void testAverageGainTestPreparation()
{
    const std::string source =
        "CM lossy test\n"
        "CE\n"
        "CM inline AGT note\n"
        "GW 1 11 -5 0 6 5 0 6 .001\n"
        "GE 1\n"
        "GN 2 0 0 0 13 .005\n"
        "LD 5 1 0 0 5.8e7 0 0\n"
        "LD 0 1 3 3 2 1e-6 0\n"
        "TL 1 3 1 9 50 0 2 .5 3 .7\n"
        "EX 0 1 6 0 1 0\n"
        "FR 0 3 0 0 7 0.1\n"
        "XQ 0\n"
        "RP 0 19 12 1000 0 0 5 30\n"
        "EN\n";
    const auto deck = necwb::analysis::prepareAverageGainTestInput(source, 7.1,
        necwb::analysis::AverageGainEnvironment::PerfectGround);
    expect(deck.find("CM lossy test") != std::string::npos
            && deck.find("CM inline AGT note") == std::string::npos
            && deck.find("GE 1\nGN 1") != std::string::npos
            && deck.find("GN 2") == std::string::npos,
        "AGT preparation normalizes comments and replaces finite ground with perfect ground");
    expect(deck.find("LD 5") == std::string::npos
            && deck.find("LD 0 1 3 3 0 1e-6 0") != std::string::npos,
        "AGT preparation removes conductor loss and zeroes resistive loads");
    expect(deck.find("TL 1 3 1 9 50 0 0 .5 0 .7") != std::string::npos,
        "AGT preparation zeroes transmission-line shunt losses");
    expect(deck.find("FR 0 1 0 0 7.1 0") != std::string::npos
            && deck.find("RP 0 91 361 1002 0 0 1 1") != std::string::npos
            && deck.find("XQ 0") == std::string::npos,
        "perfect-ground AGT uses one frequency and hemisphere averaging");

    const auto freeSpaceDeck = necwb::analysis::prepareAverageGainTestInput(source, 7.1,
        necwb::analysis::AverageGainEnvironment::FreeSpace);
    expect(freeSpaceDeck.find("GE 0") != std::string::npos
            && freeSpaceDeck.find("GN ") == std::string::npos
            && freeSpaceDeck.find("RP 0 181 361 1002 0 0 1 1") != std::string::npos,
        "free-space AGT removes ground and uses full-sphere averaging");

    const auto assessment = necwb::analysis::assessAverageGain(1.96, 2.0);
    expect(assessment.classification == necwb::analysis::AverageGainClassification::Pass
            && std::abs(assessment.normalizedGain - 0.98) < 1.0e-12
            && assessment.gainAdjustmentDb > 0.0,
        "AGT assessment normalizes perfect-ground results and reports correction direction");

    std::vector<necwb::analysis::RadiationSample> freeSpaceSamples;
    for (const auto theta : {0.0, 90.0, 180.0}) {
        for (const auto phi : {0.0, 180.0, 360.0})
            freeSpaceSamples.push_back({7.1, theta, phi, 0.0, 0.0, 0.0});
    }
    const auto freeSpaceAverage = necwb::analysis::integrateAverageGain(
        freeSpaceSamples, necwb::analysis::AverageGainEnvironment::FreeSpace);
    expect(freeSpaceAverage
            && std::abs(freeSpaceAverage->averagePowerGain - 1.0) < 1.0e-12
            && std::abs(freeSpaceAverage->solidAnglePi - 4.0) < 1.0e-12,
        "AGT integration recovers isotropic full-sphere average gain");

    std::vector<necwb::analysis::RadiationSample> groundSamples;
    for (const auto theta : {0.0, 45.0, 90.0}) {
        for (const auto phi : {0.0, 180.0, 360.0})
            groundSamples.push_back({7.1, theta, phi, 3.010299956639812, 0.0,
                3.010299956639812});
    }
    const auto groundAverage = necwb::analysis::integrateAverageGain(
        groundSamples, necwb::analysis::AverageGainEnvironment::PerfectGround);
    expect(groundAverage
            && std::abs(groundAverage->averagePowerGain - 2.0) < 1.0e-12
            && std::abs(groundAverage->solidAnglePi - 2.0) < 1.0e-12,
        "AGT integration recovers perfect-ground hemisphere average gain");
}

void testSegmentationConvergencePreparation()
{
    const std::string source =
        "CM convergence model\n"
        "CE\n"
        "CM inline convergence note\n"
        "GW 1 11 -5 0 6 5 0 6 .001\n"
        "GW 2 10 0 0 0 0 0 5 .001\n"
        "GE 0\n"
        "LD 0 1 3 9 5 0 0\n"
        "TL 1 3 2 8 50 0 0 0 0 0\n"
        "EX 0 1 6 0 1 0\n"
        "FR 0 3 0 0 7 0.1\n"
        "XQ 0\n"
        "EN\n";
    const auto prepared = necwb::analysis::prepareSegmentationConvergenceInput(
        source, 7.1, 1.5);
    expect(prepared.ok() && prepared.totalSegments == 32
            && prepared.deck.find("CM inline convergence note") == std::string::npos,
        "convergence preparation normalizes comments and scales wire segment counts");
    const auto document = necwb::nec::NecParser{}.parse(prepared.deck);
    const auto model = necwb::nec::NecModelConverter{}.convert(document).model;
    const auto setup = necwb::nec::NecSetupConverter{}.convert(document);
    expect(model.wireByTag(1) != nullptr && model.wireByTag(1)->segments == 17
            && model.wireByTag(2) != nullptr && model.wireByTag(2)->segments == 15,
        "convergence preparation preserves centered-source odd segmentation");
    expect(setup.excitations.size() == 1 && setup.excitations[0].segment == 9
            && setup.loads.size() == 1 && setup.loads[0].firstSegment == 4
            && setup.loads[0].lastSegment == 14
            && setup.transmissionLines.size() == 1
            && setup.transmissionLines[0].segment1 == 4
            && setup.transmissionLines[0].segment2 == 12,
        "convergence preparation remaps segment attachments by physical position");
    expect(setup.frequency && setup.frequency->count == 1
            && std::abs(setup.frequency->startMHz - 7.1) < 1.0e-12,
        "convergence preparation runs one selected frequency");

    const auto unsupported = necwb::analysis::prepareSegmentationConvergenceInput(
        source.substr(0, source.find("EN\n")) + "NT 1 1 2 2 1 0 0 0 0 0\nEN\n", 7.1, 1.5);
    expect(!unsupported.ok(), "convergence preparation rejects unsupported network remapping");

    const auto globalSubset = necwb::analysis::prepareSegmentationConvergenceInput(
        "GW 1 11 0 0 0 1 0 0 .001\nGE 0\nLD 0 0 3 5 5 0 0\n"
        "EX 0 1 6 0 1 0\nFR 0 1 0 0 7.1 0\nXQ 0\nEN\n", 7.1, 1.5);
    expect(!globalSubset.ok(),
        "convergence preparation rejects ambiguous global load subsets");

    const auto excessive = necwb::analysis::prepareSegmentationConvergenceInput(
        source, 7.1, 1000.0);
    expect(excessive.ok() && !excessive.warning.empty(),
        "convergence preparation flags refinements outside recommended segment limits");
}

}

void testSegmentCurrentDisplay()
{
    using necwb::analysis::SegmentCurrentResult;
    const auto segmentCurrent = [](int tag, int segment, double magnitude, double phase) {
        SegmentCurrentResult current;
        current.frequencyMHz = 14.0;
        current.wireTag = tag;
        current.segment = segment;
        current.magnitude = magnitude;
        current.phaseDegrees = phase;
        current.current = std::polar(magnitude, phase * std::numbers::pi / 180.0);
        return current;
    };
    necwb::analysis::FeedpointResult feed;
    feed.frequencyMHz = 14.0;
    feed.wireTag = 1;
    feed.segment = 1;
    feed.current = std::polar(2.0, 30.0 * std::numbers::pi / 180.0);

    // Two parallel elements entered in opposite directions carry the same physical current;
    // NEC reports them 180 degrees apart, the display must not.
    necwb::model::AntennaModel parallel;
    parallel.addWire({1, {0.0, -1.0, 0.0}, {0.0, 1.0, 0.0}, 1, 0.001, 1});
    parallel.addWire({2, {0.5, 1.0, 0.0}, {0.5, -1.0, 0.0}, 1, 0.001, 2});
    const std::array parallelCurrents{segmentCurrent(1, 1, 2.0, 30.0),
        segmentCurrent(2, 1, 1.0, -150.0)};
    const auto shown = necwb::analysis::displayedCurrents(parallelCurrents,
        std::span{&feed, 1}, parallel);
    expect(shown.feedReferenced && shown.segments.size() == 2
            && std::abs(shown.maximumMagnitude - 2.0) < 1.0e-12,
        "displayed currents keep every segment with model geometry");
    expect(!shown.segments[0].reversed && std::abs(shown.segments[0].relativePhaseDegrees) < 1.0e-9,
        "the feed segment is the phase reference");
    expect(shown.segments[1].reversed && std::abs(shown.segments[1].relativePhaseDegrees) < 1.0e-9
            && shown.segments[1].start.y < shown.segments[1].end.y,
        "a wire entered against its main axis is shown in the common direction");

    // A dipole built from two wires running outward from the center stays continuous.
    necwb::model::AntennaModel dipole;
    dipole.addWire({1, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1, 0.001, 1});
    dipole.addWire({2, {0.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, 1, 0.001, 2});
    const std::array dipoleCurrents{segmentCurrent(1, 1, 1.0, 10.0),
        segmentCurrent(2, 1, 1.0, -170.0)};
    const auto dipoleShown = necwb::analysis::displayedCurrents(dipoleCurrents, {}, dipole);
    expect(!dipoleShown.feedReferenced
            && std::abs(dipoleShown.segments[0].relativePhaseDegrees - 10.0) < 1.0e-9
            && std::abs(dipoleShown.segments[1].relativePhaseDegrees - 10.0) < 1.0e-9,
        "without a feed, phases are absolute and joined halves agree");

    necwb::analysis::DisplayedSegmentCurrent instant;
    instant.magnitude = 2.0;
    instant.relativePhaseDegrees = 60.0;
    expect(std::abs(necwb::analysis::instantaneousCurrent(instant, 0.0) - 1.0) < 1.0e-12
            && std::abs(necwb::analysis::instantaneousCurrent(instant, 30.0)) < 1.0e-12
            && std::abs(necwb::analysis::instantaneousCurrent(instant, 120.0) + 2.0) < 1.0e-12,
        "instantaneous current follows |I| cos(phase + cycle)");

    // A vertical over radials wider than it is tall must still be seen from the side.
    necwb::model::AntennaModel groundPlane;
    groundPlane.addWire({1, {0.0, 0.0, 6.0}, {0.0, 0.0, 16.0}, 1, 0.001, 1});
    groundPlane.addWire({2, {0.0, 0.0, 6.0}, {10.0, 0.0, 6.0}, 1, 0.001, 2});
    groundPlane.addWire({3, {0.0, 0.0, 6.0}, {0.0, 10.0, 6.0}, 1, 0.001, 3});
    groundPlane.addWire({4, {0.0, 0.0, 6.0}, {-10.0, 0.0, 6.0}, 1, 0.001, 4});
    groundPlane.addWire({5, {0.0, 0.0, 6.0}, {0.0, -10.0, 6.0}, 1, 0.001, 5});
    const std::array groundPlaneCurrents{segmentCurrent(1, 1, 1.0, 0.0),
        segmentCurrent(2, 1, 0.25, 180.0), segmentCurrent(3, 1, 0.25, 180.0),
        segmentCurrent(4, 1, 0.25, 180.0), segmentCurrent(5, 1, 0.25, 180.0)};
    const auto groundPlaneShown = necwb::analysis::displayedCurrents(groundPlaneCurrents, {},
        groundPlane);
    expect(necwb::analysis::dominantProjectionPlane(shown.segments)
                == necwb::geometry::ProjectionPlane::XY
            && necwb::analysis::dominantProjectionPlane(groundPlaneShown.segments)
                == necwb::geometry::ProjectionPlane::XZ,
        "the projection plane shows the most current");

    // Wires that share a tag (tag 0 is common in imported decks) are told apart by NEC's
    // absolute segment number, which runs through the wires in GW order.
    necwb::model::AntennaModel sharedTags;
    sharedTags.addWire({1, {0.0, -1.0, 0.0}, {0.0, 1.0, 0.0}, 3, 0.001, 1});
    sharedTags.addWire({1, {0.5, 1.0, 0.0}, {0.5, -1.0, 0.0}, 3, 0.001, 2});
    sharedTags.addWire({0, {1.0, -1.0, 0.0}, {1.0, 1.0, 0.0}, 2, 0.001, 3});
    sharedTags.addWire({0, {1.5, -1.0, 0.0}, {1.5, 1.0, 0.0}, 2, 0.001, 4});
    const auto necCurrent = [&segmentCurrent](int tag, int tagSegment, int necSegment,
                                double phase) {
        auto current = segmentCurrent(tag, tagSegment, 1.0, phase);
        current.necSegment = necSegment;
        return current;
    };
    const std::array sharedCurrents{necCurrent(1, 1, 1, 0.0), necCurrent(1, 2, 2, 0.0),
        necCurrent(1, 3, 3, 0.0), necCurrent(1, 4, 4, 180.0), necCurrent(1, 5, 5, 180.0),
        necCurrent(1, 6, 6, 180.0), necCurrent(0, 1, 7, 0.0), necCurrent(0, 2, 8, 0.0),
        necCurrent(0, 3, 9, 0.0), necCurrent(0, 4, 10, 0.0)};
    necwb::analysis::FeedpointResult sharedFeed;
    sharedFeed.frequencyMHz = 14.0;
    sharedFeed.wireTag = 1;
    sharedFeed.segment = 5;
    sharedFeed.current = std::polar(1.0, 180.0 * std::numbers::pi / 180.0);
    const auto sharedShown = necwb::analysis::displayedCurrents(sharedCurrents,
        std::span{&sharedFeed, 1}, sharedTags);
    const auto shownAt = [&sharedShown](int necSegment) {
        return *std::ranges::find(sharedShown.segments, necSegment,
            &necwb::analysis::DisplayedSegmentCurrent::necSegment);
    };
    expect(sharedShown.segments.size() == 10
            && shownAt(5).wireIndex == 1 && shownAt(5).segment == 2
            && std::abs(shownAt(5).start.x - 0.5) < 1.0e-12
            && shownAt(9).wireIndex == 3 && shownAt(9).segment == 1
            && std::abs(shownAt(9).start.x - 1.5) < 1.0e-12,
        "currents on wires that share a tag land on their own wire");
    // The feed is on the second tag-1 wire, which runs against its axis, so its reference
    // phase includes the 180-degree flip and both parallel elements read 0.
    expect(sharedShown.feedReferenced
            && std::abs(shownAt(2).relativePhaseDegrees) < 1.0e-9
            && std::abs(shownAt(5).relativePhaseDegrees) < 1.0e-9,
        "the feed on a shared-tag wire is found by its absolute segment");

    // A feed segment the model does not have must not borrow the first wire with its tag:
    // here that wire runs against its axis and would flip every phase by 180 degrees.
    necwb::model::AntennaModel reversedFirst;
    reversedFirst.addWire({1, {0.0, 1.0, 0.0}, {0.0, -1.0, 0.0}, 1, 0.001, 1});
    reversedFirst.addWire({1, {0.5, -1.0, 0.0}, {0.5, 1.0, 0.0}, 1, 0.001, 2});
    necwb::analysis::FeedpointResult unmatchedFeed;
    unmatchedFeed.frequencyMHz = 14.0;
    unmatchedFeed.wireTag = 1;
    unmatchedFeed.segment = 9;
    unmatchedFeed.current = std::polar(1.0, 0.0);
    const std::array unmatchedCurrents{necCurrent(1, 2, 2, 0.0)};
    const auto unmatchedShown = necwb::analysis::displayedCurrents(unmatchedCurrents,
        std::span{&unmatchedFeed, 1}, reversedFirst);
    expect(unmatchedShown.segments.size() == 1
            && std::abs(unmatchedShown.segments[0].relativePhaseDegrees) < 1.0e-9,
        "an unmatched feed segment does not take its orientation from another wire");

    // A model that no longer matches the solved deck places nothing rather than a wrong wire.
    const std::array staleCurrents{necCurrent(2, 1, 4, 0.0), necCurrent(1, 7, 11, 0.0)};
    expect(necwb::analysis::displayedCurrents(staleCurrents, {}, sharedTags).segments.empty(),
        "currents whose tag or segment does not match the model are skipped");

    // nec2++ numbers a 40-segment flat spiral 1-40 and the following GW 41-43, feeding it at
    // 42. The spiral is not drawn, but the GW's currents and feed must still be found.
    const auto spiralFirst = necwb::nec::NecModelConverter{}.convert(necwb::nec::NecParser{}.parse(
        "GH 8 40 0.05 0 0.1 0.1 0.2 0.2 0.001\nGW 1 3 0 -1 3 0 1 3 0.001\nGE 0\n")).model;
    std::vector<necwb::analysis::SegmentCurrentResult> spiralCurrents;
    for (auto segment = 1; segment <= 40; ++segment)
        spiralCurrents.push_back(necCurrent(8, segment, segment, 97.0));
    spiralCurrents.push_back(necCurrent(1, 1, 41, -82.765));
    spiralCurrents.push_back(necCurrent(1, 2, 42, -61.700));
    spiralCurrents.push_back(necCurrent(1, 3, 43, -82.765));
    necwb::analysis::FeedpointResult spiralFeed;
    spiralFeed.frequencyMHz = 14.0;
    spiralFeed.wireTag = 1;
    spiralFeed.segment = 42;
    spiralFeed.current = std::polar(1.0, -61.700 * std::numbers::pi / 180.0);
    const auto spiralShown = necwb::analysis::displayedCurrents(spiralCurrents,
        std::span{&spiralFeed, 1}, spiralFirst);
    expect(spiralShown.segments.size() == 3 && spiralShown.feedReferenced
            && spiralShown.segments[1].necSegment == 42 && spiralShown.segments[1].segment == 2
            && std::abs(spiralShown.segments[1].relativePhaseDegrees) < 1.0e-9,
        "currents after an undrawn flat spiral are still shown on their wire");
}

auto main() -> int
{
    testReferenceRunSelection();
    testSegmentCurrentDisplay();
    testHistoricalOverlayCompatibility();
    testControlPanelSummaries();
    testOptimizationBaselineFingerprint();
    testOptimizationBaselineCompatibility();
    testOptimizationPresets();
    testRoundTripPreservesSource();
    testUtf8BomParsing();
    testLegacyFreeFormatParsing();
    testSourceLineEditing();
    testSourceDocumentCache();
    testKnownCardsAreRecognized();
    testCompleteNec2CardCatalog();
    testSymbolResolution();
    testCardFieldEditingPreservesExpressions();
    testWireConversion();
    testInvalidWireIsDiagnosed();
    testGeometryScaleConversion();
    testGeneratedWireGeometryConversion();
    testGeometryTransformConversion();
    testSurfacePatchConversion();
    testValidModelCheck();
    testStaticModelAdequacyChecks();
    testCardValidation();
    testGenericKnownCardValidation();
    testGeometryCardOrdering();
    testDeckStructureValidation();
    testControlCardSequence();
    testBackendCompatibilityProfiles();
    testIncompleteModelCheck();
    testOrthographicProjection();
    testWireLookupAndWriting();
    testProjectionBoundsAndDistance();
    testLengthUnitConversions();
    testWireGaugeConversions();
    testAutoSegmentation();
    testStructuredModelSetup();
    testGroundSetup();
    testAnalysisRequests();
    testLoadsAndTransmissionLines();
    testSolverCommand();
    testRadiationSweepSolverInput();
    testNecOutputParsing();
    testOptimizationObjectives();
    testAdaptiveSearch();
    testNelderMeadSearch();
    testDifferentialEvolutionSearch();
    testFrequencyPlans();
    testAverageGainTestPreparation();
    testSegmentationConvergencePreparation();
    testSharedTagReferences();
    testUnrenderedSegmentNumbering();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
