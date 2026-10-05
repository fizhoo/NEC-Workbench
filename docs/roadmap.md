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

- `nec2c`, OpenNEC, and 4nec2 NEC2dXS process adapters with independent saved paths
- Normal runs and temporary quick sweeps without rewriting the authored model
- Durable run history with authored source, generated solver deck, output, logs,
  metadata, and detached historical review
- Impedance/SWR, segment-current, and 2D/3D radiation inspection
- Static adequacy checks, Average Gain Test, and segmentation convergence

### Optimization

- Source-backed `SY` parameters and structured-card parameter promotion
- Single-variable exhaustive sweep
- Multi-variable adaptive coordinate search, Nelder–Mead, and seeded Differential Evolution
- Shared frequency plans and weighted SWR, resistance, reactance, gain, F/B, and F/R objectives
- Per-criterion goal and band reduction, retained candidate details, and explicit
  undoable application of a selected result

### Delivery

- Automated Linux AppImage and Windows ZIP builds
- Qt-free core, plot, and optimization tests
- In-application tooltips for detailed control behavior

## Release Readiness

The next milestone is a dependable early release rather than another broad feature expansion.

1. Exercise representative real decks across supported solver backends and platforms.
2. Strengthen regression coverage around source edits, run generation, output parsing,
   detached windows, and optimization session restoration.
3. Review card support claims against actual validation, structured editing, and rendering.
4. Finish concise user-facing release notes, installation guidance, and known limitations.
5. Publish versioned Linux and Windows binaries through GitHub Releases.

## Near-Term Work

### Optimization Confidence

- Finalist sensitivity and construction-tolerance analysis
- Optional per-frequency or per-band importance and explicit pass/fail constraints
- Two-variable grid sweeps and objective heat maps
- Clearer comparison of baseline, finalist, and applied-model results
- Continue validating objective calculations and search stopping reasons with tests

Additional search algorithms should be added only when they offer a clear advantage
over the existing reproducible methods.

### Results and Validation

- CSV export for numerical result tables
- Deliberate two-run impedance/SWR comparison before broader plot overlays
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
