# Development Roadmap

This roadmap records intended development direction, not guaranteed release
dates. NEC Workbench remains solver-independent; external NEC executables are
configured by the user and are not bundled.

## Current Foundation

- Evaluate the reusable detachable Results-window prototype before applying the
  same task-window pattern to optimization monitoring.

- Raw and structured NEC source editing with validation and source mapping
- Synchronized 2D and 3D wire geometry inspection and editing
- Frequency, ground, source, load, transmission-line, and request setup
- External `nec2c` execution with durable run artifacts and history
- Impedance, SWR, current-distribution, and 2D/3D radiation results
- Historical run snapshots that restore the exact archived input deck
- Categorized Project tree with contextual Properties and editor navigation
- Solver-independent `SY` expressions with numeric NEC run-deck generation
- Ordered `GS` geometry scaling with separate display and deck-unit controls
- One-variable bounded sweeps with model-sweep or explicit-frequency SWR objectives
- Static model-adequacy checks for segmentation, thin-wire ratios, sources, and junctions
- Single-frequency lossless Average Gain Test with archived Validation results
- Segmentation convergence studies with EX, LD, and TL attachment remapping
- Linux development builds and automated Windows packaging

## Next: Model Adequacy

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

1. Show resolved values and expression errors in structured editors.
2. Apply a selected optimization candidate back into the active model.
3. Add safe expression-aware structured and graphical editing.

Parameterization is the required foundation for both model templates and
optimization. It should remain a Workbench feature rather than depending on a
particular solver's extensions.

## Then: Adaptive Optimization

1. Plot SWR, impedance, gain, efficiency, and other available metrics by candidate.
2. Add an iterative optimizer with explicit search-convergence stopping criteria.
3. Add finalist sensitivity/tolerance analysis for parameter perturbations.
4. Add gain and pattern objectives, weights, and constraints.
5. Add bounded multi-parameter optimization.
6. Add explicit Compare Runs and baseline-versus-candidate overlays.

Explicit frequency sets currently provide a simple multi-band foundation.
Named amateur bands, per-frequency weights, and pass/fail constraints remain
future refinements rather than complicating the first workflow.

The first optimizer should favor transparent, reproducible behavior over a
large collection of algorithms. Additional search methods can be added behind a
common optimizer interface later.

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
- Detached historical-results viewing that leaves the active editable model loaded
- Near-field visualization
- Optimization algorithm plugins
- Additional platform installers and signed release packages
- Broader accessibility and theme customization
