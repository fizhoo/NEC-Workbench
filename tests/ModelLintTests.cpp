#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using necwb::nec::CompatibilityProfile;
using necwb::nec::DiagnosticSeverity;
using necwb::nec::ModelCheckOptions;
using necwb::nec::ModelCheckResult;

auto failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

auto check(const std::string& source,
    CompatibilityProfile profile = CompatibilityProfile::OriginalNec2) -> ModelCheckResult
{
    return necwb::nec::NecModelChecker{}.check(necwb::nec::NecParser{}.parse(source),
        ModelCheckOptions{profile});
}

auto count(const ModelCheckResult& result, std::string_view text,
    DiagnosticSeverity severity = DiagnosticSeverity::Warning) -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::count_if(result.diagnostics,
        [text, severity](const auto& diagnostic) {
            return diagnostic.severity == severity
                && diagnostic.message.find(text) != std::string::npos;
        }));
}

auto message(const ModelCheckResult& result, std::string_view text) -> std::string
{
    const auto found = std::ranges::find_if(result.diagnostics, [text](const auto& diagnostic) {
        return diagnostic.message.find(text) != std::string::npos;
    });
    return found == result.diagnostics.end() ? std::string{} : found->message;
}

// Geometry followed by a feed and a 14 MHz frequency (wavelength about 21.2 m).
auto deck(const std::string& geometry, const std::string& control = "EX 0 1 6 0 1 0\n",
    const std::string& ground = "GE 0\n") -> std::string
{
    return geometry + ground + control + "FR 0 1 0 0 14 0\nEN\n";
}

void testUnconnectedContacts()
{
    const auto crossing = check(deck(
        "GW 1 11 -5 0 10 5 0 10 .001\n"
        "GW 2 10 0 -5 10 0 5 10 .001\n"));
    expect(count(crossing, "not connected") == 1,
        "wires that cross between segment ends are reported once");
    expect(message(crossing, "not connected").starts_with(
               "Wire tags 1 and 2 cross at about (0.00, 0.00, 10.00) m"),
        "crossing report names both tags and the location");

    const auto unconnectedTee = check(deck(
        "GW 1 11 -5 0 10 5 0 10 .001\n"
        "GW 2 5 0 0 10 0 0 15 .001\n"));
    expect(count(unconnectedTee, "Wire tag 2 ends on wire tag 1") == 1,
        "a wire end on another wire's segment interior is reported as an open T");

    const auto connectedTee = check(deck(
        "GW 1 10 -5 0 10 5 0 10 .001\n"
        "GW 2 5 0 0 10 0 0 15 .001\n"));
    expect(count(connectedTee, "NEC does not connect") == 0
            && count(connectedTee, "not connected") == 0,
        "a wire end on another wire's interior segment end is a real NEC junction");

    const auto sharedEnd = check(deck(
        "GW 1 11 -5 0 10 0 0 10 .001\n"
        "GW 2 11 0 0 10 3 0 14 .001\n"
        "GW 3 11 0 0 10 -0.2 0 15 .001\n"));
    expect(count(sharedEnd, "not connected") == 0 && count(sharedEnd, "crosses itself") == 0,
        "wires meeting at a shared endpoint, even at a sharp angle, are not reported");
}

void testLargeRadialFieldIsFast()
{
    // A vertical over 120 elevated radials of 10 segments each.
    std::ostringstream geometry;
    geometry << "GW 1 10 0 0 5 0 0 15 .001\n";
    for (auto radial = 0; radial < 120; ++radial) {
        const auto angle = radial * 3.0 * std::numbers::pi / 180.0;
        geometry << "GW " << radial + 2 << " 10 0 0 5 " << 10.0 * std::cos(angle) << ' '
                 << 10.0 * std::sin(angle) << " 5 .001\n";
    }
    const auto started = std::chrono::steady_clock::now();
    const auto radials = check(deck(geometry.str(), "EX 0 1 2 0 1 0\n"));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    expect(count(radials, "not connected") == 0,
        "radials joined at a common hub are not reported as crossings");
    expect(count(radials, "run parallel") == 0,
        "radials with matching segmentation are not reported as unaligned");
    expect(elapsed < std::chrono::seconds(1),
        "contact check stays fast on a 120-radial field");

    // A short wire across the field cuts the seven radials within about 9.5 degrees of +x.
    geometry << "GW 200 1 3 -0.5 5 3 0.5 5 .001\n";
    const auto crossed = check(deck(geometry.str(), "EX 0 1 2 0 1 0\n"));
    expect(count(crossed, "not connected") == 7,
        "a wire across a radial field reports each crossed radial once");
}

void testGroundFit()
{
    const auto buried = check(deck("GW 1 11 -5 0 -1 5 0 -1 .001\n", "EX 0 1 6 0 1 0\n",
        "GE 0\nGN 1\n"));
    expect(count(buried, "is below ground", DiagnosticSeverity::Error) == 1,
        "a wire below a defined ground is an error");
    for (const auto profile : {CompatibilityProfile::FourNec2, CompatibilityProfile::OpenNec}) {
        expect(count(check(deck("GW 1 11 -5 0 -1 5 0 -1 .001\n", "EX 0 1 6 0 1 0\n",
                             "GE 0\nGN 1\n"), profile),
                   "is below ground", DiagnosticSeverity::Error) == 1,
            "buried wires are errors for every NEC-2 family backend");
    }
    expect(count(check(deck("GW 1 11 -5 0 -1 5 0 -1 .001\n")), "below ground",
               DiagnosticSeverity::Error) == 0,
        "negative z is only a coordinate in free space");

    const auto spanning = check(deck("GW 1 11 0 0 -1 0 0 5 .001\n", "EX 0 1 6 0 1 0\n",
        "GE 1\nGN 1\n"));
    expect(count(spanning, "through the ground", DiagnosticSeverity::Error) == 1
            && count(spanning, "is below ground", DiagnosticSeverity::Error) == 0,
        "a wire passing through ground gets one spanning error");

    const auto openAtGround = check(deck("GW 1 11 0 0 0 0 0 5 .001\n"));
    expect(count(openAtGround, "Wire tag 1 ends at z = 0, but the model has no ground") == 1,
        "a free-space wire standing on z = 0 suggests a missing ground");
    expect(count(check(deck("GW 1 11 0 0 0 0 0 5 .001\n", "EX 0 1 6 0 1 0\n",
                   "GE 1\nGN 1\n")), "no ground") == 0,
        "a wire on z = 0 over a defined ground is connected to it");
    expect(count(check(deck("GW 1 11 -5 0 0 5 0 0 .001\n")), "no ground") == 0,
        "a free-space model drawn in the z = 0 plane is not reported");

    const auto lowReflection = check(deck("GW 1 11 -5 0 1 5 0 1 .001\n", "EX 0 1 6 0 1 0\n",
        "GE 0\nGN 0 0 0 0 13 0.005\n"));
    expect(count(lowReflection, "Use GN 2") == 1,
        "GN 0 with a low horizontal wire suggests Sommerfeld-Norton");
    expect(count(check(deck("GW 1 11 -5 0 1 5 0 1 .001\n", "EX 0 1 6 0 1 0\n",
                   "GE 0\nGN 2 0 0 0 13 0.005\n")), "Use GN 2") == 0,
        "GN 2 with a low horizontal wire is the recommended setup");
    expect(count(check(deck("GW 1 11 -5 0 10 5 0 10 .001\n", "EX 0 1 6 0 1 0\n",
                   "GE 0\nGN 0 0 0 0 13 0.005\n")), "Use GN 2") == 0,
        "GN 0 is acceptable for wires well above ground");
}

void testParallelAlignment()
{
    const auto offset = check(deck(
        "GW 1 11 -5 0 10 5 0 10 .001\n"
        "GW 2 10 -5 0.05 10 5 0.05 10 .001\n"));
    expect(count(offset, "Wire tags 1 and 2 run parallel 0.05 m apart") == 1,
        "closely spaced parallel wires with offset segments are reported");

    expect(count(check(deck(
                   "GW 1 11 -5 0 10 5 0 10 .001\n"
                   "GW 2 11 5 0.05 10 -5 0.05 10 .001\n")), "run parallel") == 0,
        "parallel wires with aligned segments are not reported, whatever their direction");
    expect(count(check(deck(
                   "GW 1 11 -5 0 10 5 0 10 .001\n"
                   "GW 2 10 -5 2 10 5 2 10 .001\n")), "run parallel") == 0,
        "widely spaced parallel wires are not reported");
    expect(count(check(deck(
                   "GW 1 11 -5 0 10 0 0 10 .001\n"
                   "GW 2 7 0 0 10 5 0 10 .001\n")), "run parallel") == 0,
        "collinear wires meeting end to end do not overlap");
}

void testInsertionSegments()
{
    const std::string groundPlane =
        "GW 1 10 0 0 5 0 0 15 .001\n"
        "GW 2 10 0 0 5 5 0 3 .001\n"
        "GW 3 10 0 0 5 -2.5 4.33 3 .001\n"
        "GW 4 10 0 0 5 -2.5 -4.33 3 .001\n";
    const auto junctionFeed = check(deck(groundPlane, "EX 0 1 1 0 1 0\n"));
    expect(count(junctionFeed, "EX on wire tag 1 segment 1 touches a junction of 4 segments") == 1,
        "a source at a multi-wire junction is reported");
    expect(count(check(deck(groundPlane, "EX 0 1 2 0 1 0\n")), "touches a junction") == 0,
        "a source one segment from the junction is not reported");
    expect(count(check(deck(groundPlane, "EX 0 1 2 0 1 0\nTL 2 1 1 5 50 0\n")),
               "TL end on wire tag 2 segment 1 touches a junction") == 1,
        "a transmission-line end at a multi-wire junction is reported");

    const std::string stepped =
        "GW 1 5 -5 0 10 0 0 10 .001\n"
        "GW 2 5 0 0 10 5 0 10 .005\n";
    expect(count(check(deck(stepped, "EX 0 1 5 0 1 0\n")), "adjoins a radius step") == 1,
        "a source beside a radius step is reported");
    expect(count(check(deck(stepped, "EX 0 1 4 0 1 0\n")), "radius step") == 0,
        "a source away from the radius step is not reported");

    const std::string dipole = "GW 1 11 -5 0 10 5 0 10 .001\n";
    expect(count(check(deck(dipole, "EX 0 1 6 0 1 0\nLD 0 1 1 1 50 0 0\n")),
               "LD on wire tag 1 segment 1 is on the end segment",
               DiagnosticSeverity::Information) == 1,
        "a lumped load on a free wire end is informational");
    expect(count(check(deck(dipole, "EX 0 1 6 0 1 0\nLD 0 1 3 3 50 0 0\nLD 5 1 1 11 5.8e7\n")),
               "end segment", DiagnosticSeverity::Information) == 0,
        "inner lumped loads and whole-wire conductivity are not reported");
    expect(count(check(deck("GW 1 11 0 0 0 0 0 5 .001\n", "EX 0 1 1 0 1 0\n",
                   "GE 1\nGN 1\n")), "on wire tag 1", DiagnosticSeverity::Information) == 0,
        "a base feed on a wire connected to ground is not a free end");
    expect(count(check(deck(dipole + "GW 2 1 0 0 1 0 0 1.1 .001\n",
                   "EX 0 2 1 0 1 0\nTL 1 6 2 1 50 0\n")), "on wire tag 2",
               DiagnosticSeverity::Information) == 0,
        "a one-segment source stub for a transmission line is not reported");
}

void testTransmissionLineLength()
{
    const std::string geometry =
        "GW 1 11 -5 0 10 5 0 10 .001\n"
        "GW 2 1 0 0 1 0 0 1.1 .001\n";
    const auto shortLine = check(deck(geometry, "EX 0 2 1 0 1 0\nTL 1 6 2 1 50 5\n"));
    expect(count(shortLine, "is 5 m long, but those segments are 8.95 m apart") == 1,
        "a TL shorter than the gap it spans is reported");
    expect(count(check(deck(geometry, "EX 0 2 1 0 1 0\nTL 1 6 2 1 50 0\n")), "m long") == 0,
        "a zero TL length means the straight-line distance");
    expect(count(check(deck(geometry, "EX 0 2 1 0 1 0\nTL 1 6 2 1 50\n")), "m long") == 0,
        "an omitted TL length means the straight-line distance");
    expect(count(check(deck(geometry, "EX 0 2 1 0 1 0\nTL 1 6 2 1 50 8.9\n")), "m long") == 0,
        "a TL within 1% of the gap is accepted");
    expect(count(check(deck(geometry, "EX 0 2 1 0 1 0\nTL 1 6 2 1 50 15.24\n")), "m long") == 0,
        "a TL longer than the gap is accepted");
}

}

auto main() -> int
{
    testUnconnectedContacts();
    testLargeRadialFieldIsFast();
    testGroundFit();
    testParallelAlignment();
    testInsertionSegments();
    testTransmissionLineLength();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All model lint tests passed\n";
    return EXIT_SUCCESS;
}
