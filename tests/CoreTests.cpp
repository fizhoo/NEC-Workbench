#include "analysis/SolverCommand.h"
#include "analysis/NecOutputParser.h"
#include "geometry/OrthographicProjection.h"
#include "model/AutoSegmentation.h"
#include "model/LengthUnit.h"
#include "model/WireGauge.h"
#include "nec/NecModelConverter.h"
#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"
#include "nec/NecWriter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

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

void testKnownCardsAreRecognized()
{
    const std::string source = "CM note\nCE\nGW 1 3 0 0 0 1 0 0 .001\nGE\nEX\nLD\nGN\nFR\nRP\nTL\nNT\nEN";
    const auto cards = necwb::nec::NecParser{}.parse(source).cards();
    expect(cards.size() == 12, "all known card lines are parsed");
    expect(cards[0].kind == necwb::nec::NecCardKind::Comment, "CM recognized");
    expect(cards[2].kind == necwb::nec::NecCardKind::GeometryWire, "GW recognized");
    expect(cards[11].kind == necwb::nec::NecCardKind::End, "EN recognized");
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
}

void testCardValidation()
{
    const std::string source =
        "GW 1 3 0 0 0 1 0 0 .001\n"
        "GE 0\n"
        "EX 0 1 bad 0 1 0\n"
        "FR 0 0 0 0 -14 0\n"
        "ZZ unsupported\n"
        "EX 0 1 2 0 1 0\n"
        "FR 0 1 0 0 14 0\n";
    const auto result = necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(source));
    expect(result.errorCount() == 3, "invalid EX and FR values produce errors");
    expect(result.warningCount() == 1, "unknown card produces a warning");
    expect(result.diagnostics.front().lineNumber == 3, "card diagnostic retains source line");
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

void testIncompleteModelCheck()
{
    const auto result = necwb::nec::NecModelChecker{}.check(
        necwb::nec::NecParser{}.parse("CM incomplete model\nCE\nEN\n"));
    expect(result.errorCount() == 0,
        "incomplete analysis setup remains editable rather than becoming a syntax error");
    expect(result.warningCount() == 3,
        "missing geometry, frequency, and source produce completeness warnings");
    expect(result.diagnostics[0].message.find("no valid GW") != std::string::npos,
        "incomplete model warning identifies missing wire geometry");
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
        "FR 0 101 0 0 3.00000E+00 5.00000E-02 8.00000E+00 0.00000E+00 0.00000E+00 0.00000E+00\n");
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
        "EN\n");
    const auto setup = necwb::nec::NecSetupConverter{}.convert(document);
    expect(setup.executionRequest && setup.executionRequest->option == 0,
        "structured setup reads XQ execution requests");
    expect(setup.radiationPattern && setup.radiationPattern->thetaCount == 91
            && setup.radiationPattern->phiCount == 1
            && setup.radiationPattern->thetaStep == 1.0,
        "structured setup reads normal RP angular sampling");
    expect(necwb::nec::NecWriter{}.writeExecutionCard(*setup.executionRequest) == "XQ 0",
        "execution requests write canonical XQ cards");
    expect(necwb::nec::NecWriter{}.writeRadiationPatternCard(*setup.radiationPattern)
            == "RP 0 91 1 1000 0 0 1 0 0 0",
        "radiation requests write canonical RP cards");
    expect(necwb::nec::NecModelChecker{}.check(document).errorCount() == 0,
        "valid XQ and RP requests pass model checking");

    const auto invalid = necwb::nec::NecParser{}.parse(
        "RP 0 0 2 1000 0 0 1 0 0 0\nXQ bad\n");
    expect(necwb::nec::NecModelChecker{}.check(invalid).errorCount() == 3,
        "invalid RP counts, angular steps, and XQ options are diagnosed");
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
}

void testSolverCommand()
{
    expect(necwb::analysis::isBackendRunnable("nec2"),
        "nec2 has a process adapter");
    expect(!necwb::analysis::isBackendRunnable("opennec"),
        "unimplemented backends are not reported as runnable");
    const auto command = necwb::analysis::buildSolverCommand(
        "nec2", "/usr/bin/nec2c", "model.nec", "model.out");
    expect(command.executable == "/usr/bin/nec2c",
        "solver command retains the configured executable");
    expect(command.arguments.size() == 2
            && command.arguments[0] == "-imodel.nec"
            && command.arguments[1] == "-omodel.out",
        "nec2c command uses short working-directory-relative arguments");
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
        "\n"
        " RADIATION PATTERNS\n"
        " 0 0 -999.99 -999.99 -999.99 0 0\n"
        " 90 0 2.15 -999.99 2.15 0 0 LINEAR\n"
        "\n";
    const auto result = necwb::analysis::NecOutputParser{}.parse(output);
    expect(result.feedpoints.size() == 2, "NEC output parser reads each frequency block");
    expect(result.feedpoints[0].frequencyMHz == 14.0
            && result.feedpoints[0].wireTag == 1
            && result.feedpoints[0].segment == 6,
        "feedpoint results retain frequency and source location");
    expect(result.feedpoints[0].impedance == std::complex<double>{75.0, 15.0},
        "feedpoint results retain complex impedance");
    expect(result.feedpoints[1].frequencyMHz == 14.1
            && result.feedpoints[1].impedance == std::complex<double>{50.0, 0.0},
        "parser accepts Fortran D exponent output");
    expect(std::abs(necwb::analysis::standingWaveRatio({50.0, 0.0}) - 1.0) < 1.0e-12,
        "matched 50-ohm impedance has one-to-one SWR");
    expect(necwb::analysis::standingWaveRatio({0.0, 0.0})
            == std::numeric_limits<double>::infinity(),
        "zero impedance reports infinite SWR");
    expect(result.currents.size() == 2
            && result.currents[0].wireTag == 1
            && result.currents[0].segment == 1
            && result.currents[0].magnitude == 0.010198,
        "NEC output parser reads segment current distribution rows");
    expect(result.radiation.size() == 2
            && result.radiation[1].thetaDegrees == 90.0
            && result.radiation[1].totalGainDb == 2.15,
        "NEC output parser reads radiation gain samples");
}

}

auto main() -> int
{
    testRoundTripPreservesSource();
    testKnownCardsAreRecognized();
    testWireConversion();
    testInvalidWireIsDiagnosed();
    testValidModelCheck();
    testCardValidation();
    testGeometryCardOrdering();
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
    testNecOutputParsing();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
