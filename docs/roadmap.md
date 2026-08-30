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
- Categorized Project tree with contextual Properties and editor navigation
- Linux development builds and automated Windows packaging

## Next: Parameterized Models

1. Define named model parameters using an `SY`-style syntax.
2. Parse safe arithmetic expressions and parameter references.
3. Show resolved values and expression errors in structured editors.
4. Generate a numeric NEC deck before invoking native NEC-2 backends.
5. Preserve parameterized source separately from generated run input.

Parameterization is the required foundation for both model templates and
optimization. It should remain a Workbench feature rather than depending on a
particular solver's extensions.

## Then: Sweeps and Optimization

1. Sweep one selected parameter over a bounded range.
2. Plot SWR, impedance, gain, efficiency, and other available metrics by candidate.
3. Define objectives, weights, constraints, and evaluation frequencies.
4. Add bounded multi-parameter optimization.
5. Preserve candidates as normal Analysis runs with reproducible metadata.
6. Add explicit Compare Runs and baseline-versus-candidate overlays.

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
- Near-field visualization
- Optimization algorithm plugins
- Additional platform installers and signed release packages
- Broader accessibility and theme customization
