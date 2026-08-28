#pragma once

#include "model/Point3D.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace necwb::model { class AntennaModel; }

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
    std::optional<RadiationPatternRequest> radiationPattern;
    std::vector<Excitation> excitations;
    std::vector<LoadDefinition> loads;
    std::vector<TransmissionLineDefinition> transmissionLines;
};

[[nodiscard]] auto frequencyEndMHz(const FrequencyDefinition& frequency) -> double;
[[nodiscard]] auto frequencyPointCount(int steppingMode, double startMHz,
    double endMHz, double step) -> std::optional<int>;

[[nodiscard]] auto excitationPosition(const AntennaModel& model, const Excitation& excitation)
    -> std::optional<Point3D>;
[[nodiscard]] auto wireSegmentPosition(const AntennaModel& model, int wireTag, int segment)
    -> std::optional<Point3D>;
[[nodiscard]] auto loadPosition(const AntennaModel& model, const LoadDefinition& load)
    -> std::optional<Point3D>;

}
