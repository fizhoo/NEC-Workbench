# Development Roadmap

This document records direction rather than promised release dates. NEC
Workbench remains solver-independent and does not bundle external NEC engines.

## Current Baseline

### Modeling

- One source-backed document shared by Raw Source, Structured Cards, geometry,
  model setup, parameter editing, validation, and Undo/Redo
- Editable straight-wire geometry plus read-only display of supported generated
  wires, transformations, and surface patches
- Structured frequency, source, environment, load, transmission-line, request,
  `SY`, and reference-impedance workflows
- Complete NEC-2 mnemonic recognition with unknown extensions preserved

### Analysis and Results

- Compact Home operations dashboard with reusable Model, Setup, result, and Run History windows
- `nec2c`, OpenNEC, and 4nec2 NEC2dXS process adapters with independent saved paths
- Normal runs and temporary quick sweeps without rewriting the authored model
- Durable run history with authored source, generated solver deck, output, logs,
  metadata, and detached historical review
- Impedance/SWR, segment-current, and 2D/3D radiation inspection
- One labeled historical overlay selected from Run History and shown independently
  in compatible impedance/SWR and radiation views
- Static adequacy checks, Average Gain Test, and segmentation convergence

### Optimization

- Source-backed `SY` parameters and structured-card parameter promotion
- Single-variable exhaustive sweep
- Multi-variable adaptive coordinate search, Nelder–Mead, and seeded Differential Evolution
- Shared frequency plans and weighted SWR, resistance, reactance, gain, F/B, and F/R objectives
- Per-criterion goal and band reduction, retained candidate details, and explicit
  undoable application of a selected result
- Automatic compatible baseline runs with detached candidate comparison for
  impedance, SWR, and available 2D radiation patterns

### Delivery

- Automated Linux AppImage and Windows ZIP builds
- Automated core, plot, and optimization test suites; core logic remains Qt-free
- In-application tooltips for detailed control behavior

## Current Milestone

`v0.1.0-alpha.1` established the first public Linux AppImage and Windows ZIP baseline.
The current milestone is an Alpha 2 stabilization candidate rather than broad feature
expansion.

1. Exercise representative real decks across supported solver backends and platforms.
2. Field-test the compact Home workflow, reusable windows, Run History, and historical overlays.
3. Strengthen regression coverage around source edits, run generation, output parsing,
   window restoration, and optimization session restoration.
4. Keep card-support claims synchronized with validation, structured editing, and rendering.
5. Download and smoke-test the Windows package on Windows before tagging Alpha 2.
6. Fix reproducible release defects, then prepare a focused `v0.1.0-alpha.2` release.

The first imported-deck validation audit is complete and documented in the
[NEC Validation Reference](nec-validation-reference.md). Follow-up work should focus
on verified extension profiles and confirmed corpus regressions rather than broad,
solver-specific permissiveness.

The initial dual-backend execution baseline now covers verified numeric and symbolic
Workbench decks through NEC2C and the patched OpenNEC test build, including parsing
the resulting impedance and radiation reports. Continue growing this corpus only with
documented, reproducible cases.

## Near-Term Work

### Optimization Confidence

- Finalist sensitivity and construction-tolerance analysis
- Revisit a compact, non-blocking warning when a parameter's current value falls
  outside its optimization bounds; avoid adding a persistent action bar
- Optional per-frequency or per-band importance and explicit pass/fail constraints
- Two-variable grid sweeps and objective heat maps
- Refine the one-candidate/one-baseline comparison from field testing; broader
  multi-candidate overlays remain deferred unless they demonstrate clear value
- Continue validating objective calculations and search stopping reasons with tests

Additional search algorithms should be added only when they offer a clear advantage
over the existing reproducible methods.

### Results and Validation

- CSV export for numerical result tables
- Refine one-run historical overlays from cross-platform field testing before considering
  a separate multi-run comparison workflow
- Convergence plots for impedance, gain, and pattern changes
- Input-power normalization for current and field amplitudes
- Near-field parsing and visualization

### Modeling

- Flat-spiral `GH` semantic geometry
- Additional card-specific editors where labels and constraints improve safety
- Broader attachment remapping, including supported `NT` cases
- Antenna templates and a guided antenna creator

## Longer Term

- Smith chart and transmission-line, stub, matching, and phasing calculators
- Filled or shaded 3D radiation surfaces and additional polarization views
- Signed installers and broader platform packaging
- Accessibility and theme refinement
- Carefully bounded plugin interfaces after core model, solver, and result APIs stabilize

See [NEC Card Support](nec-card-support.md) for current card-level behavior. Ideas
that are not yet scheduled remain in [Scratchpad](scratchpad.md).
