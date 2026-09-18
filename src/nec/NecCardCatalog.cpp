#include "nec/NecCardCatalog.h"

#include <array>
#include <ranges>

namespace necwb::nec {
namespace {

using enum NecCardArea;
using enum NecCardKind;
using enum NecCardSupport;

constexpr std::array catalog{
    NecCardSpec{"CM", "Comment", Comment, Comments, Understood, 0, 0},
    NecCardSpec{"CE", "End comments", Comment, Comments, Understood, 0, 0},
    NecCardSpec{"GW", "Wire", GeometryWire, Geometry, StructuredEditable, 2, 7},
    NecCardSpec{"GC", "Wire taper", GeometryOther, Geometry, Understood, 2, 3},
    NecCardSpec{"GA", "Wire arc", GeometryOther, Geometry, Understood, 2, 4},
    NecCardSpec{"GH", "Helix", GeometryOther, Geometry, Understood, 2, 7},
    NecCardSpec{"SP", "Surface patch", GeometryOther, Geometry, Understood, 2, 7},
    NecCardSpec{"SM", "Multiple-patch surface", GeometryOther, Geometry, Understood, 2, 7},
    NecCardSpec{"SC", "Patch continuation", GeometryOther, Geometry, Understood, 2, 7},
    NecCardSpec{"GM", "Move or replicate structure", GeometryOther, Geometry, Understood, 2, 7},
    NecCardSpec{"GX", "Reflect structure", GeometryOther, Geometry, Understood, 2, 7},
    NecCardSpec{"GR", "Generate cylindrical structure", GeometryOther, Geometry, Understood, 2, 7},
    NecCardSpec{"GS", "Scale structure", GeometryScale, Geometry, StructuredEditable, 2, 1},
    NecCardSpec{"GF", "Read Numerical Green's Function", GeometryOther, Geometry, Understood, 2, 7},
    NecCardSpec{"GE", "End geometry", GeometryEnd, Environment, StructuredEditable, 2, 0},
    NecCardSpec{"EK", "Extended thin-wire kernel", ControlOther, Environment, Understood, 4, 6},
    NecCardSpec{"FR", "Frequency", Frequency, FrequencySources, StructuredEditable, 4, 6},
    NecCardSpec{"GN", "Ground", Ground, Environment, StructuredEditable, 4, 6},
    NecCardSpec{"KH", "Interaction approximation range", ControlOther, Environment, Understood, 4, 6},
    NecCardSpec{"LD", "Structure loading", Load, LoadsNetworks, StructuredEditable, 4, 6},
    NecCardSpec{"EX", "Excitation", Excitation, FrequencySources, StructuredEditable, 4, 6},
    NecCardSpec{"NT", "Two-port network", Network, LoadsNetworks, Understood, 4, 6},
    NecCardSpec{"TL", "Transmission line", TransmissionLine, LoadsNetworks, StructuredEditable, 4, 6},
    NecCardSpec{"CP", "Coupling calculation", ControlOther, RequestsExecution, Understood, 4, 6},
    NecCardSpec{"EN", "End data", End, RequestsExecution, Understood, 4, 6},
    NecCardSpec{"GD", "Additional ground parameters", ControlOther, Environment, Understood, 4, 6},
    NecCardSpec{"NE", "Near electric field", ControlOther, RequestsExecution, Understood, 4, 6},
    NecCardSpec{"NH", "Near magnetic field", ControlOther, RequestsExecution, Understood, 4, 6},
    NecCardSpec{"NX", "Next structure", ControlOther, RequestsExecution, Understood, 4, 6},
    NecCardSpec{"PQ", "Charge-density print control", ControlOther, RequestsExecution, Understood, 4, 6},
    NecCardSpec{"PT", "Current print control", ControlOther, RequestsExecution, Understood, 4, 6},
    NecCardSpec{"RP", "Radiation pattern", RadiationPattern, RequestsExecution, StructuredEditable, 4, 6},
    NecCardSpec{"WG", "Write Numerical Green's Function", ControlOther, RequestsExecution, Understood, 4, 6},
    NecCardSpec{"XQ", "Execute", Execute, RequestsExecution, StructuredEditable, 4, 6},
    NecCardSpec{"SY", "Workbench symbol", Symbol, Parameters, StructuredEditable, 0, 0},
    NecCardSpec{"Z0", "Reference impedance", ReferenceImpedance, Extensions, StructuredEditable, 0, 1},
    NecCardSpec{"ZO", "Legacy reference impedance", ReferenceImpedance, Extensions, StructuredEditable, 0, 1},
};

}

auto necCardCatalog() noexcept -> std::span<const NecCardSpec>
{
    return catalog;
}

auto findNecCardSpec(std::string_view mnemonic) noexcept -> const NecCardSpec*
{
    const auto found = std::ranges::find(catalog, mnemonic, &NecCardSpec::mnemonic);
    return found == catalog.end() ? nullptr : &*found;
}

auto isGeometryCard(NecCardKind kind) noexcept -> bool
{
    return kind == NecCardKind::GeometryWire || kind == NecCardKind::GeometryScale
        || kind == NecCardKind::GeometryOther;
}

auto isControlCard(NecCardKind kind) noexcept -> bool
{
    return kind == NecCardKind::Excitation || kind == NecCardKind::Load
        || kind == NecCardKind::Ground || kind == NecCardKind::Frequency
        || kind == NecCardKind::RadiationPattern || kind == NecCardKind::Execute
        || kind == NecCardKind::TransmissionLine || kind == NecCardKind::Network
        || kind == NecCardKind::ControlOther || kind == NecCardKind::ReferenceImpedance
        || kind == NecCardKind::End;
}

}
