#include "analysis/SolverCommand.h"
#include "analysis/AdaptiveSearch.h"
#include "analysis/AverageGainTest.h"
#include "analysis/DifferentialEvolutionSearch.h"
#include "analysis/FrequencyPlan.h"
#include "analysis/NecOutputParser.h"
#include "analysis/NelderMeadSearch.h"
#include "analysis/OptimizationObjective.h"
#include "analysis/SegmentationConvergence.h"
#include "analysis/SolverInput.h"
#include "geometry/OrthographicProjection.h"
#include "model/AutoSegmentation.h"
#include "model/LengthUnit.h"
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
#include "nec/NecSymbolResolver.h"
#include "nec/NecSymbolEditor.h"
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
            && parameterized->find("SY wire_x1=-5\r\nGW 1 11 wire_x1 0 6 5 0 6 0.001")
                != std::string::npos,
        "a fixed numeric card field can be promoted to an inserted SY parameter");
    const auto parameterizedResolution = parameterized
        ? necwb::nec::NecSymbolResolver{}.resolve(*parameterized)
        : necwb::nec::SymbolResolution{};
    expect(parameterizedResolution.ok()
            && parameterizedResolution.generatedDeck.find(
                "GW 1 11 -5 0 6 5 0 6 0.001") != std::string::npos,
        "a promoted field resolves back to the original numeric NEC card");
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
        "a parameter link can be replaced by its resolved numeric value");
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
    expect(spiral.model.empty() && spiral.issues.empty(),
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
        "RP 0 181 361 1000 0.000 0.000 1.000 1.000\n");
    const auto shortSetup = necwb::nec::NecSetupConverter{}.convert(shortPattern);
    expect(necwb::nec::NecModelChecker{}.check(shortPattern).errorCount() == 0
            && shortSetup.radiationPatterns.size() == 1
            && shortSetup.radiationPatterns[0].thetaCount == 181
            && shortSetup.radiationPatterns[0].phiCount == 361,
        "RP accepts omitted optional distance and normalization fields");

    const auto invalid = necwb::nec::NecParser{}.parse(
        "RP 0 0 2 1000 0 0 1 0 0 0\nXQ bad\nZO -50\n");
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
            && !elevationMetrics.frontToBackDb,
        "elevation-cut metrics retain tied peaks, interpolate HPBW, and require a physical back direction");

    std::vector<necwb::analysis::RadiationCutPoint> horizontalCut;
    for (auto phi = 0; phi < 360; ++phi)
        horizontalCut.push_back({static_cast<double>(phi), 62.0,
            static_cast<double>(phi), phi == 0 ? 8.0 : -2.0});
    const auto offHorizonMetrics = necwb::analysis::radiationCutMetrics(horizontalCut, true);
    expect(offHorizonMetrics.valid && !offHorizonMetrics.frontToBackDb
            && offHorizonMetrics.sampledBeamwidthDegrees
            && *offHorizonMetrics.sampledBeamwidthDegrees == 0.0
            && offHorizonMetrics.interpolatedBeamwidthDegrees
            && std::abs(*offHorizonMetrics.interpolatedBeamwidthDegrees - 0.6) < 1.0e-12,
        "off-horizon azimuth cut wraps HPBW across the seam without inventing F/B");
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
    directional.radiation.front().frequencyMHz = 14.1;
    auto forwardOnlyObjective = directionalObjective;
    forwardOnlyObjective.frontToBackWeight = 0.0;
    expect(!necwb::analysis::evaluateOptimizationObjective(
            directional, forwardOnlyObjective),
        "directional gain is unavailable when the requested frequency is absent");

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

auto main() -> int
{
    testRoundTripPreservesSource();
    testSourceLineEditing();
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

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
