# NEC Validation Reference

NEC Workbench validates the resolved numeric deck that it will use for model checks.
The authored source remains unchanged. `SY` definitions are evaluated first, while
`Z0`/`ZO` remain Workbench metadata and are omitted from strict solver input.

Validation is deliberately narrower than solver emulation: Workbench reports known
structural, reference, sequencing, and adequacy problems, but the selected solver is
authoritative for numerical execution.

## Backend Profiles

| Feature | Original NEC-2 / NEC2C | 4nec2 NEC2dXS | OpenNEC |
|---|---|---|---|
| Standard NEC-2 cards | Validated | Validated | Validated |
| `SY` symbols and formulas | Expanded to numeric fields | Expanded to numeric fields | Expanded to numeric fields |
| `Z0` / `ZO` reference impedance | Workbench metadata; not sent as NEC-2 cards | Workbench metadata | Workbench metadata |
| `LD 6` LC trap | Error: not original NEC-2 | Recognized, but currently blocked because Workbench does not perform the full 4nec2 conversion | Accepted |
| Unknown extension card | Preserved with warning | Preserved with warning | Preserved with warning |

The profile follows **Setup → Solver**. Backend-specific diagnostics identify the
selected target by name and the Model Check report links back to Solver settings.

Direct **4nec2 NEC2dXS** execution also has a backend-specific output boundary:
recognized numeric cards in the generated `model.nec` use canonical single-space
separators. Workbench accepts tabs and commas while editing, but NEC2dXS does not
recognize tabs as free-field delimiters. This conversion changes only the solver copy;
`model.source.nec` retains the authored formatting, and numeric text is not rounded.

The following known extension families are not yet classified by dedicated rules:
`LD 7`, `GN 3`, `EX 6`, percentage segment selectors, invisible-geometry tag
conventions, and OpenNEC-only syntax. Until those rules are implemented, Raw Source
preservation and the backend's own diagnostics remain authoritative.

## Severity

- **Error** — the generated model is structurally invalid, contains an invalid known
  reference/value, cannot be resolved, or is incompatible with the selected backend.
  Errors block analysis and optimization.
- **Warning** — the deck can be preserved or executed, but its meaning is incomplete,
  backend-dependent, numerically questionable, or easy to misread.
- **Converted** — Workbench performed an intentional source-to-solver conversion,
  such as resolving `SY` definitions.

Workbench does not silently reorder cards or rewrite the authored model to make an
invalid sequence pass.

## Parsing and Deck Structure

Workbench accepts whitespace or commas between fields, leading `+` signs, omitted
trailing zero/default fields on supported cards, and `FR` count `0` as one frequency.
A single-frequency `FR` may omit its increment. A multi-frequency `FR` requires a
positive increment. A leading UTF-8 byte-order mark is preserved but does not hide
the first card mnemonic.

Leading `CM` comments must end with `CE` before geometry starts. Geometry must end
with one `GE`. A complete deck must contain one final `EN`; only blank lines may
follow it.

## Card Validation Matrix

**Structured** means a dedicated Workbench editor exists. **Understood** means the
card is recognized, preserved, generically exposed, and receives field-shape checks.
Graphical rendering is documented separately in [NEC Card Support](nec-card-support.md).

| Card | Support | Current validation |
|---|---|---|
| `CM`, `CE` | Understood | Leading comment-block boundary and placement |
| `GW` | Structured | Tag/segment and endpoint fields, nonzero length, positive radius or following valid `GC`; omitted radius defaults to zero |
| `GC` | Understood | Numeric field shape; taper ratios and radii when attached to zero-radius `GW` |
| `GA` | Structured | Tag, positive segment count/radius, distinct arc angles |
| `GH` | Structured | Helix dimensions and endpoint radii; flat spirals are preserved but not rendered, still count toward absolute segment numbers, and get a reliability warning |
| `SP`, `SM`, `SC` | Structured | Shape codes, required continuation, nonzero-area corners, positive grid counts |
| `GM` | Understood | Tag increment, repetition count, and transform fields |
| `GX` | Understood | Tag increment, reflection flags, and geometry reflection constraints |
| `GR` | Understood | Tag increment and positive copy count |
| `GS` | Structured | Positive scale factor and source-order application |
| `GF` | Understood | Numeric field shape; backend operation remains authoritative |
| `GE` | Structured | Required geometry boundary, uniqueness, and ordering |
| `EK`, `KH` | Understood | Numeric field shape and execution-state participation |
| `FR` | Structured | Mode, count, positive start, and sweep increment rules |
| `GN` | Structured | NEC-2 types `-1` through `2`; finite-ground material values |
| `LD` | Structured | Type range, wire/segment references, numeric values, and `LD 6` profile handling; trailing values default to zero and tag `0` is accepted |
| `EX` | Structured | Field types and voltage-source wire/segment references; omitted trailing imaginary voltage defaults to zero |
| `NT` | Understood | Numeric field shape and execution-state participation |
| `TL` | Structured | Endpoint references and nonzero impedance; omitted length/shunt values default to zero |
| `CP` | Understood | Numeric field shape |
| `EN` | Understood | Required once as the final nonblank card |
| `GD` | Understood | Numeric field shape and requirement for a later `RP` |
| `NE`, `NH` | Understood | Numeric field shape and automatic-frequency execution rules |
| `NX` | Understood | Numeric field shape |
| `PQ`, `PT` | Understood | Numeric field shape and execution-state participation |
| `RP` | Structured | Counts, angular steps, field types, excitation state, and repeated-sweep behavior |
| `WG` | Understood | Numeric field shape; backend file operation remains authoritative |
| `XQ` | Structured | Integer option, excitation state, and control-sequence behavior |
| `SY` | Structured | Name, expression, dependency resolution, finite result, and generated numeric expansion |
| `Z0`, `ZO` | Structured | Positive reference impedance metadata |

## Control-Sequence Rules

NEC-2 uses execution state rather than one universal control-card order. Workbench
tracks cards that change calculation state and the requests that execute it.

- `XQ` and `RP` initiate execution; single-frequency `NE`/`NH` may also execute.
- During automatic frequency stepping, `NE`/`NH` require a following `XQ` or `RP`.
- A request after a completed multi-frequency loop runs at the final frequency unless
  another `FR` starts a new loop; Workbench reports that situation as a warning.
- State-changing cards after the final execution request are errors because no later
  request uses them.
- `GD` without a later `RP` is an error.

## Static Adequacy Checks

Static adequacy checks run against the highest analysis frequency once the deck has a
valid `FR`. They are reported in the **Model adequacy** category. These findings are
engineering guidance, not proof of convergence or correctness. Use Average Gain Test
and Segmentation Convergence for numerical validation.

| Check | Severity | Threshold |
|---|---|---|
| Electrically long or very short segments | Warning | Segment above 0.1 λ, or below 0.001 λ |
| Segment-length/diameter ratio | Warning | Ratio of 4 or less |
| Adjoining segment lengths at a junction | Warning | Ratio above 2:1 |
| Large multiwire junction | Warning | More than 30 wires |
| Even-segment center-fed wire | Warning | `EX` on either middle segment of an even-segment wire |
| One-way segment connection | Error | A segment end reaches another within its own 0.001-length tolerance, but the other end does not reach back, so NEC-2's `TRIO` junction walk meets a free end or never returns. NEC-2 stops with "TRIO - SEGMENT CONNECTION ERROR"; nec2c runs out of memory on a ring that never returns. NEC-2 family backends only |
| Wires that cross or touch without connecting | Warning | Closest approach of two segments within the sum of their radii, unless they share a segment end or are one segment apart. A free wire end resting on another wire between its segment ends is reported as an unconnected T |
| Wire below ground | Error | Any point below z = 0 with `GN` 0, 1, or 2, or with `GE 1` (NEC-2 family backends) |
| Wire passing through ground | Error | One end above z = 0 and another point below it, under the same conditions |
| Segment lying in the ground plane | Error | Both segment ends at z = 0 with `GE 1`; NEC-2 stops on this |
| Wire end at z = 0 that is not tied to ground | Warning | A free wire end at z = 0 on a wire that does not lie in the z = 0 plane, unless the deck has both `GN` and `GE 1`. The message covers the three cases: no ground, `GN` with `GE 0`/`-1`, and `GE 1` without `GN`. Reported once on `GE` |
| `GN 0` with low horizontal wires | Warning | Segment centers below 0.1 λ at the lowest swept frequency and within 30° of horizontal; suggests `GN 2` |
| Unaligned closely spaced parallel wires | Warning | Straight wires within 5° of parallel, overlapping by at least a segment, closer than both max(10 × the larger radius, 0.01 λ) and the shorter segment length, but not touching, with segment junctions offset by more than 10% of the shorter segment. Applies to NEC-2 family backends only |
| `EX`, `LD`, or `TL` end beside a radius step | Warning | The segment touches a junction where radii differ by more than 10% |
| `EX`, `LD`, or `TL` end at a multiwire junction | Information | The segment touches a junction of more than two segments. NEC-2 allows this (only `EX 5` needs a simple two-segment junction), so it is a note |
| `EX` or `LD` on a free wire end | Information | The segment touches an unconnected wire end that `GE 1` does not tie to ground. One-segment wires (`TL`/`NT` stubs), `LD 5`, and segment-range loads are skipped |
| Transmission line shorter than its gap | Warning | Explicit nonzero `TL` length more than 1% below the straight-line distance between the two segment centers. A zero or omitted length is not checked, since NEC then uses that distance |

The 0.1 λ `GN 0` height, the 5° parallel angle, the close-spacing limits, the 10%
alignment and radius-step limits, and the 1% `TL` tolerance are named constants in
`NecModelChecker.cpp`. They are guidance from the NEC-2 literature, not hard solver
limits. The parallel-alignment spacing is capped at a segment length because the NEC-2
user's guide asks for aligned segments only when parallel wires are "very close". The
buried-wire, one-way connection, and parallel-alignment checks reflect NEC-2 kernel
behavior; every current backend uses that kernel, and these checks are gated on it so NEC-4 class
backends can skip them later.

Connections follow NEC-2's `CONECT` tolerance: two segment ends are joined when
|dx| + |dy| + |dz| is within 0.001 of the segment length, and an end that close to
z = 0 is on the ground. NEC applies each segment's own length, so a long segment can
reach a short one that does not reach back. Workbench counts ends as joined only when
both segments agree. It also replays `CONECT` and `TRIO` exactly: each end keeps one
pointer to the first matching end, searching onward from the next segment number, and
`TRIO` follows those pointers around each junction. A walk that meets a free end, or
repeats segments without returning within 30 steps, is the one-way connection error
above. The junction, contact, ground, and insertion checks all use this rule.

Ground handling follows NEC-2's `CONECT`: `GN` alone defines the ground (`GN -1` or no
`GN` means free space), and only `GE 1` ties wire ends at z = 0 to the ground image.
With `GE 0` or `GE -1`, such an end stays open and NEC forces its current to zero.
A later `GN` replaces an earlier one, so the ground checks use the `GN` in effect at
each execution request (`XQ`, `RP`, `NE`, or `NH`), or the last `GN` when there is none.
A deck that solves over ground in any request is checked against that ground.

`TL` lengths are compared in meters, as NEC reads them; a `GS` card scales the geometry
but not the `TL` length.

## Imported-Deck Audit

The 2026-10-13 audit checked 131 source decks from the OpenNEC examples, the local
NEC validation deck set, and Workbench examples against all three profiles after
`SY` resolution.

- 112 decks were error-free.
- 56 decks produced no errors or warnings.
- The audit found and fixed valid omitted trailing fields on `GW`/`GC`, `EX`, `LD`,
  and `TL`, plus comma-delimited fields passing through symbol preprocessing.
- The remaining 19 error decks are concentrated in advanced 4nec2/OpenNEC formula
  and inline-comment syntax, intentionally invalid execution tests, missing/followed
  `EN` cards, or model-specific reference/geometry cases requiring individual review.

This is a regression baseline, not a claim that every imported deck is valid NEC-2.
Corpus findings should become automated tests only after the deck behavior is verified
against NEC-2 documentation and at least one compatible solver.

## Solver and Output Regression Baseline

The 2026-10-14 stabilization pass executed 22 verified numeric decks through both
NEC2C and the patched OpenNEC test build. The corpus covered free space, perfect and
finite ground, loads, `TL`, `NT`, `GX`, frequency sweeps, radiation requests, and
sequential `FR`/`RP`/`XQ` execution. Workbench successfully parsed impedance and
radiation data from all 44 reports.

Four additional Workbench examples using `SY` expressions were expanded to numeric
solver decks, executed through both backends, and parsed successfully. Known-invalid
or backend-specific legacy files remain classified separately rather than being made
valid through silent source rewriting.

## Broad Deck Regression — 2026-10-23

The Alpha 2 Linux regression pass audited all 113 decks in the OpenNEC example
corpus through Workbench symbol resolution and validation, then exercised the
accepted decks with available NEC2C and OpenNEC builds.

- Workbench accepted 96 decks without errors under each profile represented by this
  corpus. All seven NEC2C execution failures were rejected by the Original NEC-2
  profile; no invalid NEC2C deck silently passed validation.
- Workbench parsed every finite NEC2C and OpenNEC result report produced by the
  corpus. Two OpenNEC reports containing only `NaN` engineering values were
  correctly not exposed as usable results.
- A combined OpenNEC build containing both the transmission-line allocation fixes
  and the later NEC-2 sequencing fixes matched the original Fortran reference on
  eight focused `FR`/`RP`/`XQ`/`TL` execution probes.
- The standalone OpenNEC sequencing branch did not include the earlier
  transmission-line allocation fixes. Three otherwise valid log-periodic example
  decks failed there with repeated memory-reallocation errors but passed with the
  combined build.
- OpenNEC still returned one feedpoint result per frequency for tested decks with
  two simultaneous `EX` sources, while NEC2C and the original Fortran reference
  returned both. Workbench's output parser retains multiple source results when the
  backend supplies them; this remains an OpenNEC backend compatibility issue.

This pass verifies Workbench validation, solver-deck handoff, and finite-result
parsing. It does not claim numerical equivalence for every legacy example. The
4nec2 NEC2dXS executable is a Windows-only check: its generated input format is
covered by automated tests, and the Alpha 2 Windows package was smoke-tested with the
real executable.
