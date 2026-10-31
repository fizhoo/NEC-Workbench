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
| `GH` | Structured | Helix dimensions and endpoint radii; flat spirals are preserved but not rendered |
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

Static adequacy warnings currently cover electrically long segments, conservative
segment-length/diameter ratios, adjoining segment-length and radius discontinuities,
large multiwire junctions, and even-segment center-fed wires. These findings are
engineering guidance, not proof of convergence or correctness. Use Average Gain Test
and Segmentation Convergence for numerical validation.

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
