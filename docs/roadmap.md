# Development Roadmap

This roadmap records intended development direction, not guaranteed release
dates. NEC Workbench remains solver-independent; external NEC executables are
configured by the user and are not bundled.

## Current Foundation

- Raw and structured NEC source editing with validation and source mapping
- One canonical, context-labeled source-backed Undo/Redo history across raw,
  structured, geometry, setup, and analysis-request edits
- Synchronized 2D and 3D wire geometry inspection and editing
- Frequency, ground, source, load, transmission-line, and request setup
- External `nec2c`, OpenNEC, and 4nec2 NEC2dXS execution with durable run artifacts and history
- Impedance, SWR, current-distribution, and 2D/3D radiation results
- Historical run snapshots that restore the exact archived input deck
- Reusable detached Results and optimization-candidate windows with foreground restoration
- Categorized Project tree with synchronized selection and editor navigation
- Solver-independent `SY` expressions with numeric NEC run-deck generation
- Ordered `GS` geometry scaling with separate display and deck-unit controls
- One-variable bounded sweeps with model-sweep or explicit-frequency SWR objectives
- Shared candidate evaluator for parameter sweeps and future optimizer algorithms
- Candidate objective plots with exact weighted SWR, resistance, reactance, gain, F/B, and F/R contributions
- Forward-gain, physical front-to-back, and rear-cut front-to-rear objectives using focused RP samples
- Per-criterion Minimize, Maximize, Target, and Good Enough goals for SWR/R/X/Gain/F/B/F/R
- Independent Minimum, Average, and Maximum frequency reduction per objective criterion
- Candidate min/average/max performance summaries with archived objective reconstruction
- Multi-variable adaptive coordinate refinement with per-parameter bounds and tolerances
- Bounded multi-variable Nelder–Mead search using the shared candidate evaluator
- Seeded bounded multi-variable Differential Evolution with reproducible populations
- Explicit, undoable application of the best candidate values to their active `SY` definitions
- Model-level `Z0` feed reference shared by SWR results and optimizer defaults
- Shared frequency plans for optimization and model, single, explicit-list, or continuous RP requests
- Temporary linear/logarithmic Quick Frequency Sweeps with optional all-frequency RP output
- Static model-adequacy checks for segmentation, thin-wire ratios, sources, and junctions
- Single-frequency lossless Average Gain Test with archived Validation results
- Segmentation convergence studies with EX, LD, and TL attachment remapping
- Linux development builds and automated Windows packaging
- Complete NEC-2 card recognition with centralized categories, fixed-field validation,
  safe source preservation, and a published support matrix
- Read-only semantic 2D/3D geometry for `GA`, helical `GH`, and tapered `GW`/`GC`,
  with ordered `GS`, `GM`, `GX`, and `GR` operations and segment-aware attachments

## Next: Geometry Card Semantics

`GM`, `GX`, and `GR` wire transformations now expand in authored order while the
original cards remain unchanged. The remaining sequence is:

1. Represent `SP`, `SM`, and `SC` surface patches without forcing them into wire models.
2. Add flat-spiral `GH` semantics and dedicated editors only where card-specific labels
   and constraints add value.
3. Verify generated decks with `nec2c` examples for every newly expanded card.

Card support distinguishes **Preserved**, **Understood**, and **Structured Editable**
instead of claiming that every recognized card already has complete graphical semantics.
See `docs/nec-card-support.md` for the current matrix.

## Later: Model Adequacy

Static checks are only the first layer. Following Cebik's guidance, AGT and
convergence are necessary but not sufficient tests of model adequacy. The next
sequence is:

1. Plot impedance, gain, and pattern changes across segmentation levels.
2. Add safe remapping for supported `NT` network references.
3. Revalidate optimization finalists with the converged model.

Adequacy findings should warn and explain rather than impose universal pass/fail
rules. The modeling purpose determines whether a remaining numerical difference
is operationally significant.

## Parameterized Models

Workbench recognizes `SY` declarations, evaluates safe arithmetic and earlier
parameter references, validates the resolved model, reports source-line
diagnostics, and produces a numeric solver deck. Each run retains both the
authored parameterized source and the generated NEC input. The remaining
sequence is:

1. Extend the clarified expression and resolved-value presentation into
   structured editors.
2. Add safe expression-aware structured and graphical editing.

Parameterization is the required foundation for both model templates and
optimization. It should remain a Workbench feature rather than depending on a
particular solver's extensions.

## Later: Optimization Analysis

1. Add finalist sensitivity/tolerance analysis for parameter perturbations.
2. Add optional frequency/band weights and pass/fail constraints.
3. Add two-variable grid sweeps and objective heat maps.
4. Add explicit Compare Runs and baseline-versus-candidate overlays.

Explicit frequency sets and editable amateur-band presets provide the initial
multi-band foundation. Per-frequency weights and pass/fail constraints remain
future refinements rather than complicating the first workflow.

Parameter Sweep, Adaptive Optimize, Nelder–Mead, and Differential Evolution now share the same variables,
frequency plans, objectives, candidate artifacts, and Apply actions. Further methods
should be added only when they offer a clear advantage over these reproducible baselines.
Candidate review now includes per-frequency impedance and directional metrics,
gain/F/B/F/R plots, extrema frequencies, and goal-aware scoring guidance.
Normal and historical Radiation results now provide the same compact forward
gain/F/B/F/R versus-frequency inspection without entering Optimize.

## Modeling and Analysis Improvements

- Add optional filled 3D radiation surfaces while retaining the auditable sample mesh
- Normalize attachment positions for safe `LD`, `TL`, `NT`, and additional EX remapping
- Add more geometry-card editors and semantic geometry primitives
- Add radiation polarization and additional field-component views
- Improve filled radiation surfaces and export options
- Add CSV export for numerical and sweep results
- Add antenna templates and a guided antenna creator

## Solver Backends

- Keep `nec2c` as the reference external backend
- Validate OpenNEC and NEC2dXS original-format output against a broader real-deck corpus
- Support user-provided NEC-4/NEC-5-compatible executables where their command and output formats can be adapted safely
- Keep backend invocation and output parsing separate from model editing

## Parking Lot

- Multi-run comparison workspace
- Near-field visualization
- Optimization algorithm plugins
- Additional platform installers and signed release packages
- Broader accessibility and theme customization
