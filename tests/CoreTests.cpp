#include "analysis/SolverCommand.h"
#include "analysis/AverageGainTest.h"
#include "analysis/NecOutputParser.h"
#include "analysis/OptimizationObjective.h"
#include "analysis/SegmentationConvergence.h"
#include "analysis/SolverInput.h"
#include "geometry/OrthographicProjection.h"
#include "model/AutoSegmentation.h"
#include "model/LengthUnit.h"
#include "model/WireGauge.h"
#include "nec/DeckGeometryUnits.h"
#include "nec/NecCardFieldEditor.h"
#include "nec/NecModelConverter.h"
#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"
#include "nec/NecSymbolResolver.h"
#include "nec/NecWriter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
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
    const std::string source = "CM note\nCE\nSY length=1\nGW 1 3 0 0 0 1 0 0 .001\nGS 0 0 1\nGE\nEX\nLD\nGN\nFR\nRP\nTL\nNT\nEN";
    const auto document = necwb::nec::NecParser{}.parse(source);
    const auto cards = document.cards();
    expect(cards.size() == 14, "all known card lines are parsed");
    expect(cards[0].kind == necwb::nec::NecCardKind::Comment, "CM recognized");
    expect(cards[2].kind == necwb::nec::NecCardKind::Symbol, "SY recognized");
    expect(cards[3].kind == necwb::nec::NecCardKind::GeometryWire, "GW recognized");
    expect(cards[4].kind == necwb::nec::NecCardKind::GeometryScale, "GS recognized");
    expect(cards[13].kind == necwb::nec::NecCardKind::End, "EN recognized");
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
    expect(resolution.definitions[0].expression == "5"
            && resolution.definitions[0].adjustable,
        "plain numeric assignments are retained and marked adjustable");
    expect(!resolution.definitions[1].adjustable && !resolution.definitions[2].adjustable,
        "calculated and symbol-derived expressions remain read-only");
    expect(resolution.definitions[3].name == "segments" && resolution.definitions[3].value == 21.0,
        "later assignments can use arithmetic and earlier symbols");
    const auto scientificLiteral = necwb::nec::NecSymbolResolver{}.resolve(
        "SY radius=1.0e-3, offset=+2.5\n");
    expect(scientificLiteral.ok()
            && scientificLiteral.definitions.size() == 2
            && scientificLiteral.definitions[0].adjustable
            && scientificLiteral.definitions[1].adjustable,
        "signed and scientific numeric assignments remain adjustable");
    expect(resolution.generatedDeck.find("SY ") == std::string::npos,
        "generated numeric deck omits Workbench symbol declarations");
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

    const auto shortPattern = necwb::nec::NecParser{}.parse(
        "RP 0 181 361 1000 0.000 0.000 1.000 1.000\n");
    const auto shortSetup = necwb::nec::NecSetupConverter{}.convert(shortPattern);
    expect(necwb::nec::NecModelChecker{}.check(shortPattern).errorCount() == 0
            && shortSetup.radiationPattern
            && shortSetup.radiationPattern->thetaCount == 181
            && shortSetup.radiationPattern->phiCount == 361,
        "RP accepts omitted optional distance and normalization fields");

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
        "RP 0 19 12 1000 0 0 10 30 0 0\nEN\n";
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
            && prepared.find("CM inline geometry note") == std::string::npos,
        "solver input retains the NEC comment block and omits Workbench inline comments");
    expect(prepared.find("GN 2 0 0 0 13 .005\nFR 0 3 0 0 14 0.25\nXQ 0") != std::string::npos,
        "center-only radiation retains the fast native impedance sweep");
    expect(countOccurrences(prepared, "RP 0 19 12 1000") == 1
            && prepared.find("FR 0 1 0 0 14.25 0\nRP") != std::string::npos,
        "center-only radiation requests one pattern at the sweep midpoint");

    const auto representative = necwb::analysis::prepareSolverInput(source, setup,
        necwb::analysis::RadiationSweepMode::RepresentativeFrequencies);
    expect(countOccurrences(representative, "RP 0 19 12 1000") == 3
            && representative.find("FR 0 1 0 0 14 0\nRP") != std::string::npos
            && representative.find("FR 0 1 0 0 14.25 0\nRP") != std::string::npos
            && representative.find("FR 0 1 0 0 14.5 0\nRP") != std::string::npos,
        "representative radiation requests start, center, and end patterns");

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
    expect(everyFrequency.find("FR 0 3 0 0 14 0.25") == std::string::npos
            && everyFrequency.find("XQ 0") == std::string::npos
            && countOccurrences(everyFrequency, "RP 0 19 12 1000") == 3,
        "exhaustive radiation sweeps execute one pattern per frequency");

    auto logarithmic = setup;
    logarithmic.frequency->steppingMode = 1;
    logarithmic.frequency->startMHz = 10.0;
    logarithmic.frequency->step = 2.0;
    const auto logarithmicPrepared = necwb::analysis::prepareSolverInput(source, logarithmic,
        necwb::analysis::RadiationSweepMode::EveryFrequency);
    expect(logarithmicPrepared.find("FR 0 1 0 0 10 0") != std::string::npos
            && logarithmicPrepared.find("FR 0 1 0 0 20 0") != std::string::npos
            && logarithmicPrepared.find("FR 0 1 0 0 40 0") != std::string::npos,
        "radiation sweeps expand multiplicative frequencies");

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
        " AVERAGE POWER GAIN: 9.97119E-01 - SOLID ANGLE USED IN AVERAGING: (+4.0000)*PI STERADIANS\n"
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
    expect(result.currents.size() == 3
            && result.currents[0].wireTag == 1
            && result.currents[0].segment == 1
            && result.currents[0].magnitude == 0.010198,
        "NEC output parser reads segment current distribution rows");
    expect(result.currents[2].wireTag == 2 && result.currents[2].segment == 1
            && result.currents[2].magnitude == 0.0070711,
        "global NEC current segments map to local tag-relative segments");
    expect(result.radiation.size() == 4
            && result.radiation[1].thetaDegrees == 90.0
            && result.radiation[1].totalGainDb == 2.15
            && result.radiation[1].polarizationSense
                == necwb::analysis::PolarizationSense::Linear,
        "NEC output parser reads radiation gain samples");
    expect(result.radiation[3].frequencyMHz == 14.2
            && result.radiation[3].totalGainDb == 2.25,
        "NEC output parser retains each radiation sweep frequency");
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
            && std::abs(metrics.frontToBackDb - 10.0) < 1.0e-12,
        "radiation metrics report peak direction and front-to-back ratio");
}

void testOptimizationObjectives()
{
    const std::vector<necwb::analysis::FeedpointResult> feedpoints{
        {.frequencyMHz = 7.0, .impedance = {50.0, 0.0}},
        {.frequencyMHz = 7.1, .impedance = {75.0, 0.0}},
        {.frequencyMHz = 7.2, .impedance = {100.0, 0.0}},
    };
    const auto maximum = necwb::analysis::evaluateOptimizationObjective(feedpoints,
        {.kind = necwb::analysis::OptimizationObjectiveKind::MaximumSwr,
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

auto main() -> int
{
    testRoundTripPreservesSource();
    testKnownCardsAreRecognized();
    testSymbolResolution();
    testCardFieldEditingPreservesExpressions();
    testWireConversion();
    testInvalidWireIsDiagnosed();
    testGeometryScaleConversion();
    testValidModelCheck();
    testStaticModelAdequacyChecks();
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
    testRadiationSweepSolverInput();
    testNecOutputParsing();
    testOptimizationObjectives();
    testAverageGainTestPreparation();
    testSegmentationConvergencePreparation();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
