# Development Roadmap

This document records direction rather than promised release dates. NEC
Workbench stays solver-independent: it ships nec2c so that it works out of the box,
runs every engine as a separate program, and can use others the user installs.

## Current Baseline

### Modeling

- One source-backed document shared by Raw Source, the card tables, geometry,
  model setup, parameter editing, validation, and Undo/Redo
- One main window: the geometry editor, NEC Deck, full result views and the validation
  studies take turns in the center, with a slider for each `SY` variable under the model
  tree; the optimizer has a window of its own
- The card tables show long numbers at 6 significant figures while the
  deck keeps full precision
- Editable straight-wire geometry plus read-only display of supported generated
  wires, transformations, and surface patches
- Structured frequency, source, environment, load, transmission-line, request,
  `SY`, and reference-impedance workflows
- Complete NEC-2 mnemonic recognition with unknown extensions preserved

### Analysis and Results

- Toolbar, and a model tree with per-item rows, model-quality studies, and a properties
  pane that hosts the setup and wire editors
- Results column with one selected frequency, key numbers (including F/R), live
  thumbnails, and full result views in the center of the main window
- Bundled `nec2c` 1.3.3 in both packages, plus OpenNEC, 4nec2 NEC2dXS, and the user's own
  `nec2c` with independent saved paths; the shipped examples open from Open Example…
- Normal runs and temporary quick sweeps without rewriting the authored model
- Durable run history with authored source, generated solver deck, output, logs,
  metadata, editable run notes, read-only historical Run Review, and Open Run Folder
- Impedance/SWR and 2D/3D radiation inspection
- Currents on a 2D view of the antenna as magnitude, feed-relative phase, or an
  animated RF cycle, with a common straight-wire direction, along-wire plots, and a
  segment table; the 2D and 3D geometry views and the 3D pattern colour the wires by
  current
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
- Automatic compatible baseline runs with a candidate review window comparing
  impedance, SWR, and available 2D radiation patterns
- Unified search setup that states why a run cannot start, and cached pattern
  results so switching objective presets is immediate

### Delivery

- Automated Linux AppImage and Windows ZIP builds (Windows without a console window)
- Optimized `RelWithDebInfo` default build type
- Automated core, plot, and optimization test suites; core logic remains Qt-free
- In-application tooltips for detailed control behavior

## Current Milestone

`v0.1.0-alpha.1` established the first public Linux AppImage and Windows ZIP baseline.
`v0.1.0-alpha.2` delivered run references and notes, the currents-on-antenna view, and a
faster optimizer.

`v0.1.0-alpha.3` replaces Alpha 2's Home control panel and separate work and result
windows with one main window: the model tree, variables and properties pane on the
left; the geometry, NEC Deck, full result views and the validation studies in the
center; the results column on the right; and the bottom panel for Problems, the Run
log, Runs and Raw output. The optimizer, its candidate review and historical Run Review
are separate windows. It also ships nec2c and the examples, so it works out of the box.

Next steps:

- Smith chart of the feedpoint impedance, with a measured `.s1p` file as an overlay
- Matching: transmission-line, stub and lumped matching from the feedpoint impedance
- An engine adapter layer, so each engine's command, input, capabilities and model
  checks live in one place and further engines plug in without touching the rest
- A parallel optimizer that solves several candidates at once
- Field-test the single-window workflow across solver backends and platforms, and keep
  regression coverage and card-support claims in step with the code

The first imported-deck validation audit is complete and documented in the
[NEC Validation Reference](nec-validation-reference.md). Follow-up work should focus
on verified extension profiles and confirmed corpus regressions rather than broad,
solver-specific permissiveness.

The Linux broad-deck pass now covers all 113 OpenNEC example decks through Workbench
validation plus available NEC2C and OpenNEC execution. Workbench blocked every deck
that failed NEC2C and parsed every finite result report. Focused sequencing probes
also confirm the combined OpenNEC fixes; remaining OpenNEC multiple-source behavior
is documented as a backend issue.

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
- Currents follow-ups: phase along each wire in Along Wire, and a selectable phase
  reference for multi-source arrays
- Near-field parsing and visualization

### Modeling

- Flat-spiral `GH` semantic geometry
- Additional card-specific editors where labels and constraints improve safety
- Broader attachment remapping, including supported `NT` cases
- Antenna templates and a guided antenna creator

## Longer Term

- Standalone transmission-line, stub, and phasing calculators
- Additional polarization views
- Signed installers and broader platform packaging
- Accessibility and theme refinement
- Carefully bounded plugin interfaces after core model, solver, and result APIs stabilize

See [NEC Card Support](nec-card-support.md) for current card-level behavior. Ideas
that are not yet scheduled remain in [Scratchpad](scratchpad.md).
