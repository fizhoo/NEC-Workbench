# NEC Card Support

NEC Workbench distinguishes three levels of card support:

- **Preserved** — source text survives opening, editing, saving, and run-snapshot archiving.
- **Understood** — Workbench identifies the card, assigns it to the correct NEC section,
  validates fixed integer/numeric field types, and exposes existing fields in the
  structured card view.
- **Structured Editable** — a dedicated editor provides card-specific labels, choices,
  validation, or model behavior.

Every standard NEC-2 mnemonic is recognized. Unknown solver extensions are still
preserved exactly and remain editable in Raw Source.

## Geometry Cards

| Card | Purpose | Support | Graphical geometry |
|---|---|---|---|
| `GW` | Wire | Structured Editable | Yes |
| `GC` | Tapered wire radius | Understood | Yes, as a tapered `GW` path |
| `GA` | Wire arc | Structured Editable | Yes |
| `GH` | Helix or spiral | Structured Editable | Yes for helices; flat spirals preserved but not rendered |
| `SP` | Surface patch | Structured Editable | Yes |
| `SM` | Multiple-patch surface | Structured Editable | Yes, expanded into grid cells |
| `SC` | Patch continuation | Structured Editable | Yes, with its parent `SP`/`SM` |
| `GM` | Move or replicate structure | Understood | Yes, in authored order |
| `GX` | Reflect structure | Understood | Yes, including combined reflection planes |
| `GR` | Generate cylindrical structure | Understood | Yes, around the Z axis |
| `GS` | Scale structure | Structured Editable | Applied in source order to wire and surface geometry |
| `GF` | Read Numerical Green's Function | Understood | No; backend-dependent |
| `GE` | End geometry | Structured Editable | Boundary understood |

The solver receives authored geometry cards unchanged. Workbench expands `GA`, helical
`GH`, `GW`/`GC` taper combinations, and ordered `GM`/`GX`/`GR` transformations into
semantic paths for selection, fitting, attachment markers, and 2D/3D display. Generated
or transformed paths are read-only so graphical editing cannot silently replace their
source operations. `SP` arbitrary and shaped patches, `SM` patch grids, and their `SC`
continuations remain separate surface primitives and appear as translucent polygons in
2D, 3D, Dashboard, and radiation antenna overlays. Patch geometry is currently
read-only outside Raw Source and Structured Cards.

## Program Control Cards

| Card | Purpose | Support |
|---|---|---|
| `EK` | Extended thin-wire kernel | Understood |
| `FR` | Frequency | Structured Editable |
| `GN` | Ground | Structured Editable |
| `KH` | Interaction approximation range | Understood |
| `LD` | Structure loading | Structured Editable |
| `EX` | Excitation | Structured Editable for voltage sources |
| `NT` | Two-port network | Understood |
| `TL` | Transmission line | Structured Editable |
| `CP` | Coupling calculation | Understood |
| `EN` | End data | Understood |
| `GD` | Additional ground parameters | Understood |
| `NE` | Near electric field request | Understood |
| `NH` | Near magnetic field request | Understood |
| `NX` | Next structure | Understood |
| `PQ` | Charge-density print control | Understood |
| `PT` | Current print control | Understood |
| `RP` | Radiation pattern | Structured Editable |
| `WG` | Write Numerical Green's Function | Understood; backend-dependent |
| `XQ` | Execute | Structured Editable |

## Workbench and Compatibility Cards

| Card | Purpose | Support |
|---|---|---|
| `SY` | Workbench parameter/expression | Structured Editable |
| `Z0` | Workbench/xnec2c reference impedance | Structured Editable |
| `ZO` | Legacy spelling of reference impedance | Structured Editable |
| `CM`, `CE` | Comments and comment boundary | Understood; Raw Source editing |

`SY`, `Z0`, and `ZO` are normalized or removed as appropriate when Workbench creates a
numeric deck for a strict NEC-2 backend. The authored source and archived source snapshot
retain them.
