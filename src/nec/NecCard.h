#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace necwb::nec {

enum class NecCardKind {
    Blank,
    Comment,
    Symbol,
    GeometryWire,
    GeometryScale,
    GeometryOther,
    GeometryEnd,
    Excitation,
    Load,
    Ground,
    Frequency,
    RadiationPattern,
    Execute,
    TransmissionLine,
    Network,
    ControlOther,
    ReferenceImpedance,
    End,
    Unknown
};

struct NecCard {
    NecCardKind kind{NecCardKind::Unknown};
    std::string mnemonic;
    std::vector<std::string> fields;
    std::string sourceText;
    std::size_t lineNumber{};
};

}
