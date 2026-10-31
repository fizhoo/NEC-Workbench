#pragma once

#include "model/Point3D.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace necwb::model { class AntennaModel; struct Wire; }

namespace necwb::model {

struct FrequencyDefinition {
    int steppingMode{};
    int count{1};
    double startMHz{14.175};
    double step{0.0};
    std::size_t sourceLine{};

    auto operator==(const FrequencyDefinition&) const -> bool = default;
};

struct Excitation {
    int type{};
    int wireTag{};
    int segment{1};
    double magnitude{1.0};
    double phaseDegrees{};
    std::size_t sourceLine{};

    auto operator==(const Excitation&) const -> bool = default;
};

enum class GroundType {
    FreeSpace = -1,
    ReflectionApproximation = 0,
    Perfect = 1,
    SommerfeldNorton = 2
};

struct GroundDefinition {
    GroundType type{GroundType::FreeSpace};
    double relativePermittivity{13.0};
    double conductivity{0.005};
    int geometryGroundFlag{1};
    std::size_t sourceLine{};
    std::size_t geometryEndSourceLine{};

    auto operator==(const GroundDefinition&) const -> bool = default;
};

struct ExecutionRequest {
    int option{};
    std::size_t sourceLine{};

    auto operator==(const ExecutionRequest&) const -> bool = default;
};

struct ReferenceImpedanceDefinition {
    double ohms{50.0};
    std::size_t sourceLine{};

    auto operator==(const ReferenceImpedanceDefinition&) const -> bool = default;
};

struct RadiationPatternRequest {
    int thetaCount{91};
    int phiCount{1};
    double thetaStart{};
    double phiStart{};
    double thetaStep{1.0};
    double phiStep{};
    std::size_t sourceLine{};

    auto operator==(const RadiationPatternRequest&) const -> bool = default;
};

struct LoadDefinition {
    int type{};
    int wireTag{};
    int firstSegment{};
    int lastSegment{};
    double value1{};
    double value2{};
    double value3{};
    std::size_t sourceLine{};
    auto operator==(const LoadDefinition&) const -> bool = default;
};

struct TransmissionLineDefinition {
    int wireTag1{};
    int segment1{};
    int wireTag2{};
    int segment2{};
    double characteristicImpedance{50.0};
    double lengthMeters{};
    double shuntReal1{};
    double shuntImaginary1{};
    double shuntReal2{};
    double shuntImaginary2{};
    std::size_t sourceLine{};
    auto operator==(const TransmissionLineDefinition&) const -> bool = default;
};

struct ModelSetup {
    std::optional<FrequencyDefinition> frequency;
    std::optional<GroundDefinition> ground;
    std::optional<ExecutionRequest> executionRequest;
    std::optional<ReferenceImpedanceDefinition> referenceImpedance;
    std::vector<RadiationPatternRequest> radiationPatterns;
    std::vector<Excitation> excitations;
    std::vector<LoadDefinition> loads;
    std::vector<TransmissionLineDefinition> transmissionLines;
};

[[nodiscard]] auto referenceImpedanceOhms(const ModelSetup& setup) -> double;

[[nodiscard]] auto frequencyEndMHz(const FrequencyDefinition& frequency) -> double;
[[nodiscard]] auto frequencyPointCount(int steppingMode, double startMHz,
    double endMHz, double step) -> std::optional<int>;

struct SegmentReference {
    const Wire* wire{};
    // Segment within the wire, counted from end 1.
    int segment{};
};

// How NEC reads a tag and segment pair on EX, LD, TL and NT cards: the m-th segment among
// all wires with that tag, in wire order, so wires may share a tag. Tag 0 means absolute
// segment number m.
[[nodiscard]] auto referencedSegment(const AntennaModel& model, int wireTag, int segment)
    -> std::optional<SegmentReference>;
// Segments a tag can reach under that rule: all wires with the tag, or the whole model for
// tag 0. Zero when no wire carries the tag.
[[nodiscard]] auto referencedSegmentCount(const AntennaModel& model, int wireTag) -> int;
// True when more than one wire carries the same tag, including tag 0.
[[nodiscard]] auto hasSharedWireTags(const AntennaModel& model) -> bool;
// True when segment numbering is not per wire: wires share a tag, or EX, TL, or a ranged LD
// uses tag 0 (an absolute segment number). Tools that remap segments wire by wire cannot
// handle these references yet.
[[nodiscard]] auto hasCrossWireSegmentReferences(const AntennaModel& model,
    const ModelSetup& setup) -> bool;

[[nodiscard]] auto excitationPosition(const AntennaModel& model, const Excitation& excitation)
    -> std::optional<Point3D>;
[[nodiscard]] auto wireSegmentPosition(const AntennaModel& model, int wireTag, int segment)
    -> std::optional<Point3D>;
[[nodiscard]] auto loadPosition(const AntennaModel& model, const LoadDefinition& load)
    -> std::optional<Point3D>;

}
