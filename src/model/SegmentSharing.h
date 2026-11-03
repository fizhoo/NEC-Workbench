#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <cstddef>
#include <string>
#include <vector>

namespace necwb::model {

// A wire shorter than this at the highest analysis frequency has so much reactance of its own
// that a load in series with it, on the segment of a TL end or NT port, has little effect.
inline constexpr double kElectricallyShortWavelengths = 0.05;

enum class InsertionKind {
    Source,
    Load,
    LineEnd,
    NetworkPort
};

// An EX voltage source, lumped LD load, TL end, or NT port placed on one segment.
struct InsertionObject {
    InsertionKind kind{InsertionKind::Source};
    // Position among the setup's excitations, loads, lines, or networks, counted from 1 as the
    // Setup tables list them.
    int number{};
    // TL end or NT port, 1 or 2; 0 for sources and loads.
    int end{};
    std::size_t sourceLine{};

    auto operator==(const InsertionObject&) const -> bool = default;
};

// A segment carrying more than one insertion object, and how NEC connects them. NEC-2 places
// EX sources, TL ends, and NT ports across the segment's gap, in parallel with one another;
// an LD load is an impedance in series inside the segment (NEC-2 User's Guide, NT and TL card
// notes).
struct SharedSegment {
    std::size_t wireIndex{};
    int wireTag{};
    // Segment within the wire, counted from end 1.
    int wireSegment{};
    // Segment number as an EX, LD, TL, or NT card names it: the m-th among wires with this
    // tag, or the absolute number for tag 0.
    int necSegment{};
    // NT/TL group of the ports listed; -1 when no port shares the segment.
    int networkGroup{-1};
    std::vector<InsertionObject> objects;
    // Wire length in wavelengths at the highest analysis frequency; 0 without a frequency.
    double wireWavelengths{};
    bool electricallyShort{};
    // Plain-language account of how NEC connects the objects.
    std::string description;
};

// Every segment that carries more than one insertion object. Distributed loads (LD 2, 3, 5)
// are left out. Ports from different NT/TL groups never act together, so a segment shared
// with ports of two groups yields one entry per group.
[[nodiscard]] auto describeSharedSegments(const AntennaModel& model, const ModelSetup& setup)
    -> std::vector<SharedSegment>;

// "EX 1", "LD 2", "TL 1 end 2", "NT 3 port 1".
[[nodiscard]] auto insertionObjectName(const InsertionObject& object) -> std::string;
// Short value text for a lumped load, such as "300 Ω" or "series R 5 Ω, L 2 µH".
[[nodiscard]] auto loadValueSummary(const LoadDefinition& load) -> std::string;
// True for LD types that put one lumped impedance on each segment they cover.
[[nodiscard]] auto isLumpedLoad(const LoadDefinition& load) -> bool;

}
