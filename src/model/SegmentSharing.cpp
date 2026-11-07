#include "model/SegmentSharing.h"

#include "model/WireGeometry.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace necwb::model {
namespace {

constexpr auto kSpeedOfLightMegameters = 299.792458;

auto concise(double value) -> std::string
{
    std::ostringstream stream;
    stream << std::setprecision(4) << value;
    return stream.str();
}

// "A", "A and B", or "A, B, and C".
auto joined(const std::vector<std::string>& names) -> std::string
{
    std::string text;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) {
            if (names.size() > 2) text += ',';
            text += index + 1 == names.size() ? " and " : " ";
        }
        text += names[index];
    }
    return text;
}

// The number an EX, LD, TL, or NT card uses for this segment: counted through earlier wires
// with the same tag, or through every earlier wire for tag 0.
auto necSegmentNumber(const AntennaModel& model, const Wire& wire, int wireSegment) -> int
{
    auto number = wireSegment;
    for (const auto& block : model.segmentBlocks()) {
        if (block.wire == &wire) break;
        if (wire.tag == 0 || block.tag == wire.tag) number += std::max(block.segments, 0);
    }
    return number;
}

auto describe(const SharedSegment& shared, const ModelSetup& setup, double frequencyMHz)
    -> std::string
{
    std::vector<std::string> ports;
    std::vector<std::string> sources;
    std::vector<std::string> loads;
    auto lineEnds = 0;
    auto networkPorts = 0;
    auto parallelLoad = false;
    for (const auto& object : shared.objects) {
        const auto name = insertionObjectName(object);
        switch (object.kind) {
        case InsertionKind::Source:
            sources.push_back(name);
            break;
        case InsertionKind::Load: {
            const auto& load = setup.loads[static_cast<std::size_t>(object.number - 1)];
            loads.push_back(name + " (" + loadValueSummary(load) + ")");
            parallelLoad = parallelLoad || load.type == 1;
            break;
        }
        case InsertionKind::LineEnd:
            ++lineEnds;
            ports.push_back(name);
            break;
        case InsertionKind::NetworkPort:
            ++networkPorts;
            ports.push_back(name);
            break;
        }
    }
    const auto singularPort = ports.size() == 1;
    const auto portNoun = lineEnds != 0 && networkPorts != 0 ? std::string("the lines and networks")
        : lineEnds != 0 ? std::string(singularPort ? "the line" : "the lines")
        : std::string(singularPort ? "the network" : "the networks");

    std::vector<std::string> sentences;
    if (!ports.empty()) {
        sentences.push_back(joined(ports) + (singularPort ? " connects across this gap."
                : " connect across this gap, in parallel with each other."));
    }
    if (!sources.empty()) {
        sentences.push_back(joined(sources) + (sources.size() == 1 ? " drives " : " drive ")
            + (ports.empty() ? std::string("this gap.")
                             : "the same gap, in parallel with " + portNoun + "."));
    }
    if (!loads.empty()) {
        const auto singularLoad = loads.size() == 1;
        auto sentence = joined(loads) + (singularLoad ? " is" : " are")
            + " in series inside the segment";
        if (!ports.empty()) {
            sentence += ", so " + portNoun + (singularPort ? " sees " : " see ")
                + (singularLoad ? "the load" : "the loads") + " plus this wire's own impedance";
        } else if (!sources.empty()) {
            sentence += ", in series with the source (a Thevenin source impedance)";
        }
        sentences.push_back(sentence + ".");
        if (!ports.empty() && shared.electricallyShort) {
            sentences.push_back("This wire is only " + concise(shared.wireWavelengths)
                + " wavelength long at " + concise(frequencyMHz)
                + " MHz, so its own reactance swamps the load.");
        }
        if (parallelLoad) {
            sentences.push_back(
                "LD type 1 is a parallel RLC, but the RLC as a whole still sits in series in "
                "the wire.");
        }
    }

    auto text = shared.wireTag != 0
        ? "Wire tag " + std::to_string(shared.wireTag) + ", segment "
            + std::to_string(shared.necSegment) + ":"
        : "Segment " + std::to_string(shared.necSegment) + " (tag 0):";
    for (const auto& sentence : sentences) text += ' ' + sentence;
    return text;
}

}

auto isLumpedLoad(const LoadDefinition& load) -> bool
{
    return load.type == 0 || load.type == 1 || load.type == 4 || load.type == 6;
}

auto insertionObjectName(const InsertionObject& object) -> std::string
{
    const auto number = std::to_string(object.number);
    switch (object.kind) {
    case InsertionKind::Source:
        return "EX " + number;
    case InsertionKind::Load:
        return "LD " + number;
    case InsertionKind::LineEnd:
        return "TL " + number + " end " + std::to_string(object.end);
    case InsertionKind::NetworkPort:
        return "NT " + number + " port " + std::to_string(object.end);
    }
    return {};
}

auto loadValueSummary(const LoadDefinition& load) -> std::string
{
    if (load.type == 4) {
        if (load.value2 == 0.0) return concise(load.value1) + " Ω";
        return concise(load.value1) + (load.value2 < 0.0 ? " − j" : " + j")
            + concise(std::abs(load.value2)) + " Ω";
    }
    if (load.type == 0 || load.type == 1) {
        // NEC leaves out an RLC element whose value is zero.
        std::vector<std::string> parts;
        if (load.value1 != 0.0) parts.push_back("R " + concise(load.value1) + " Ω");
        if (load.value2 != 0.0) parts.push_back("L " + concise(load.value2 * 1.0e6) + " µH");
        if (load.value3 != 0.0) parts.push_back("C " + concise(load.value3 * 1.0e12) + " pF");
        auto text = std::string(load.type == 0 ? "series" : "parallel");
        if (parts.empty()) return text + " RLC with no elements";
        for (std::size_t index = 0; index < parts.size(); ++index)
            text += (index == 0 ? " " : ", ") + parts[index];
        return text;
    }
    if (load.type == 6) return "4nec2 trap";
    return "type " + std::to_string(load.type);
}

auto describeSharedSegments(const AntennaModel& model, const ModelSetup& setup)
    -> std::vector<SharedSegment>
{
    struct Occupants {
        // Sources and loads act with the ports of every group.
        std::vector<InsertionObject> common;
        std::map<int, std::vector<InsertionObject>> portsByGroup;
    };
    std::map<std::pair<std::size_t, int>, Occupants> segments;
    const auto wires = model.wires();
    const auto occupants = [&](int wireTag, int segment) -> Occupants* {
        const auto reference = referencedSegment(model, wireTag, segment);
        if (!reference || reference->wire == nullptr) return nullptr;
        const auto index = static_cast<std::size_t>(reference->wire - wires.data());
        return &segments[{index, reference->segment}];
    };

    for (std::size_t index = 0; index < setup.excitations.size(); ++index) {
        const auto& excitation = setup.excitations[index];
        if (auto* found = occupants(excitation.wireTag, excitation.segment)) {
            found->common.push_back({InsertionKind::Source, static_cast<int>(index + 1), 0,
                excitation.sourceLine});
        }
    }
    for (std::size_t index = 0; index < setup.loads.size(); ++index) {
        const auto& load = setup.loads[index];
        if (!isLumpedLoad(load)) continue;
        // Same range rule as loadPosition(): a zero first or last segment means the wire end.
        const auto count = referencedSegmentCount(model, load.wireTag);
        const auto first = std::max(load.firstSegment, 1);
        const auto last = load.lastSegment == 0 ? count : std::min(load.lastSegment, count);
        for (auto segment = first; segment <= last; ++segment) {
            if (auto* found = occupants(load.wireTag, segment)) {
                found->common.push_back({InsertionKind::Load, static_cast<int>(index + 1), 0,
                    load.sourceLine});
            }
        }
    }
    const auto addPort = [&](InsertionKind kind, std::size_t index, int end, int wireTag,
                             int segment, int group, std::size_t sourceLine) {
        if (auto* found = occupants(wireTag, segment)) {
            found->portsByGroup[group].push_back(
                {kind, static_cast<int>(index + 1), end, sourceLine});
        }
    };
    for (std::size_t index = 0; index < setup.transmissionLines.size(); ++index) {
        const auto& line = setup.transmissionLines[index];
        addPort(InsertionKind::LineEnd, index, 1, line.wireTag1, line.segment1, line.group,
            line.sourceLine);
        addPort(InsertionKind::LineEnd, index, 2, line.wireTag2, line.segment2, line.group,
            line.sourceLine);
    }
    for (std::size_t index = 0; index < setup.networks.size(); ++index) {
        const auto& network = setup.networks[index];
        addPort(InsertionKind::NetworkPort, index, 1, network.wireTag1, network.segment1,
            network.group, network.sourceLine);
        addPort(InsertionKind::NetworkPort, index, 2, network.wireTag2, network.segment2,
            network.group, network.sourceLine);
    }

    const auto frequencyMHz = setup.frequency
        ? std::max(setup.frequency->startMHz, frequencyEndMHz(*setup.frequency)) : 0.0;
    std::vector<SharedSegment> shared;
    for (const auto& [key, found] : segments) {
        const auto& wire = wires[key.first];
        auto entry = SharedSegment{};
        entry.wireIndex = key.first;
        entry.wireTag = wire.tag;
        entry.wireSegment = key.second;
        entry.necSegment = necSegmentNumber(model, wire, key.second);
        if (frequencyMHz > 0.0) {
            entry.wireWavelengths = wireLength(wire) * frequencyMHz / kSpeedOfLightMegameters;
            entry.electricallyShort = entry.wireWavelengths < kElectricallyShortWavelengths;
        }
        const auto emit = [&](int group, const std::vector<InsertionObject>& ports) {
            if (found.common.size() + ports.size() < 2) return;
            auto value = entry;
            value.networkGroup = group;
            value.objects = found.common;
            value.objects.insert(value.objects.end(), ports.begin(), ports.end());
            value.description = describe(value, setup, frequencyMHz);
            shared.push_back(std::move(value));
        };
        if (found.portsByGroup.empty()) emit(-1, {});
        for (const auto& [group, ports] : found.portsByGroup) emit(group, ports);
    }
    return shared;
}


auto openNetworkPorts(const AntennaModel& model, const ModelSetup& setup)
    -> std::vector<OpenNetworkPort>
{
    using Segment = std::pair<const Wire*, int>;
    const auto segmentOf = [&model](int wireTag, int segment) -> std::optional<Segment> {
        const auto reference = referencedSegment(model, wireTag, segment);
        if (!reference || reference->wire == nullptr) return std::nullopt;
        return Segment{reference->wire, reference->segment};
    };
    std::set<Segment> sourced;
    for (const auto& excitation : setup.excitations) {
        if (excitation.type != 0) continue;
        if (const auto segment = segmentOf(excitation.wireTag, excitation.segment))
            sourced.insert(*segment);
    }
    std::set<std::pair<int, Segment>> lineEnds;
    for (const auto& line : setup.transmissionLines) {
        if (const auto end = segmentOf(line.wireTag1, line.segment1))
            lineEnds.emplace(line.group, *end);
        if (const auto end = segmentOf(line.wireTag2, line.segment2))
            lineEnds.emplace(line.group, *end);
    }
    // One network equation per segment and group: admittance on each port voltage.
    struct Equation {
        std::map<Segment, std::complex<double>> coefficients;
        std::vector<OpenNetworkPort> ports;
    };
    std::map<std::pair<int, Segment>, Equation> equations;
    for (std::size_t index = 0; index < setup.networks.size(); ++index) {
        const auto& network = setup.networks[index];
        const auto first = segmentOf(network.wireTag1, network.segment1);
        const auto second = segmentOf(network.wireTag2, network.segment2);
        if (!first || !second) continue;
        const std::complex y11{network.y11Real, network.y11Imaginary};
        const std::complex y12{network.y12Real, network.y12Imaginary};
        const std::complex y22{network.y22Real, network.y22Imaginary};
        auto& port1 = equations[{network.group, *first}];
        port1.coefficients[*first] += y11;
        port1.coefficients[*second] += y12;
        auto& port2 = equations[{network.group, *second}];
        port2.coefficients[*second] += y22;
        port2.coefficients[*first] += y12;
        const auto number = static_cast<int>(index + 1);
        if (*first == *second) {
            port1.ports.push_back(
                {number, 0, network.wireTag1, network.segment1, network.sourceLine});
        } else {
            port1.ports.push_back(
                {number, 1, network.wireTag1, network.segment1, network.sourceLine});
            port2.ports.push_back(
                {number, 2, network.wireTag2, network.segment2, network.sourceLine});
        }
    }
    std::vector<OpenNetworkPort> open;
    for (const auto& [key, equation] : equations) {
        if (sourced.contains(key.second) || lineEnds.contains(key)) continue;
        if (std::ranges::all_of(equation.coefficients,
                [](const auto& entry) { return entry.second == 0.0; }))
            open.insert(open.end(), equation.ports.begin(), equation.ports.end());
    }
    std::ranges::sort(open, {}, [](const auto& port) {
        return std::pair{port.sourceLine, port.port};
    });
    return open;
}

}
