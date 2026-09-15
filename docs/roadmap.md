# Development Roadmap

This roadmap records intended development direction, not guaranteed release
dates. NEC Workbench remains solver-independent; external NEC executables are
configured by the user and are not bundled.

## Current Foundation

- Raw and structured NEC source editing with validation and source mapping
- Synchronized 2D and 3D wire geometry inspection and editing
- Frequency, ground, source, load, transmission-line, and request setup
- External `nec2c` execution with durable run artifacts and history
- Impedance, SWR, current-distribution, and 2D/3D radiation results
- Historical run snapshots that restore the exact archived input deck
- Categorized Project tree with synchronized selection and editor navigation
- Solver-independent `SY` expressions with numeric NEC run-deck generation
- Ordered `GS` geometry scaling with separate display and deck-unit controls
- One-variable bounded sweeps with model-sweep or explicit-frequency SWR objectives
- Shared candidate evaluator for parameter sweeps and future optimizer algorithms
- Candidate objective plots with exact weighted SWR, resistance, and reactance contributions
- Multi-variable adaptive coordinate refinement with per-parameter bounds and tolerances
- Bounded multi-variable Nelder–Mead search using the shared candidate evaluator
- Explicit, undoable application of the best candidate values to their active `SY` definitions
- Model-level `Z0` feed reference shared by SWR results and optimizer defaults
- Shared frequency plans for optimization and model, single, explicit-list, or continuous RP requests
- Static model-adequacy checks for segmentation, thin-wire ratios, sources, and junctions
- Single-frequency lossless Average Gain Test with archived Validation results
- Segmentation convergence studies with EX, LD, and TL attachment remapping
- Linux development builds and automated Windows packaging

## Next: NEC Card Coverage

1. Inventory the complete NEC-2 card set and publish a support matrix.
2. Ensure valid unsupported cards are preserved unchanged through editing and saving.
3. Add parsing, validation, project-tree identity, and structured fields in practical groups.
4. Verify generated decks with `nec2c` examples for every newly understood card.

Card support will distinguish **Preserved**, **Understood**, and **Structured Editable**
instead of claiming that every preserved extension has a dedicated editor. This coverage
pass is followed by release stabilization, regression models, and packaging.

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
2. Add gain and pattern objectives, weights, and constraints.
3. Add two-variable grid sweeps and objective heat maps.
4. Add explicit Compare Runs and baseline-versus-candidate overlays.

Explicit frequency sets and editable amateur-band presets provide the initial
multi-band foundation. Per-frequency weights and pass/fail constraints remain
future refinements rather than complicating the first workflow.

Parameter Sweep, Adaptive Optimize, and Nelder–Mead now share the same variables,
frequency plans, objectives, candidate artifacts, and Apply actions. Further methods
should be added only when they offer a clear advantage over these reproducible baselines.

## Modeling and Analysis Improvements

- Normalize attachment positions for safe `LD`, `TL`, `NT`, and additional EX remapping
- Add more geometry-card editors and semantic geometry primitives
- Add radiation polarization and additional field-component views
- Improve filled radiation surfaces and export options
- Add CSV export for numerical and sweep results
- Add antenna templates and a guided antenna creator

## Solver Backends

- Keep `nec2c` as the reference external backend
- Add an OpenNEC adapter after the main modeling and optimization interfaces stabilize
- Support user-provided NEC-4/NEC-5-compatible executables where their command and output formats can be adapted safely
- Keep backend invocation and output parsing separate from model editing

## Parking Lot

- Multi-run comparison workspace
- Near-field visualization
- Optimization algorithm plugins
- Additional platform installers and signed release packages
- Broader accessibility and theme customization
