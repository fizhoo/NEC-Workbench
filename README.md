# NEC Workbench

NEC Workbench is a cross-platform desktop environment for creating, inspecting,
and eventually simulating Numerical Electromagnetics Code antenna models. It is
not a solver: NEC-2, NEC-4, and NEC-5 executables remain external backends behind
a solver-independent application interface.

The current foundation provides:

- A C++20 core model for points, wires, and antenna models
- A lossless line-oriented NEC document representation
- Recognition of common NEC cards while preserving unsupported cards
- Parsing and writing with original whitespace, comments, and line endings
- Semantic conversion of `GW` cards into wire model objects
- A Qt 6 NEC editor with line numbers, highlighting, and clickable model diagnostics
- Raw and structured NEC Source tabs with add/delete, card-specific columns, source mapping, and contextual properties
- A dockable engineering workbench shell with project, properties, diagnostics, and solver-output panels
- Home, Model, Analysis, Results, and Optimize workspaces in one main window
- Model groups Geometry, Parameters, Sources, Loads & Transmission Lines, Environment, and NEC Deck views
- A live dashboard combining the shared NEC document, interactive 3D renderer, model summary, and quick results
- Temporary linear or logarithmic frequency sweeps without modifying the authored NEC deck
- An editable `GW` wire-card table synchronized with source, geometry, project selection, and undo/redo
- A Model Parameters editor for adding, updating, deleting, and resolving `SY` expressions
- Categorized, schema-specific Structured Cards navigation over the authoritative NEC deck
- An undoable Apply Best action that writes one or more optimization winners back through `SY`
- Selectable metric/imperial display units with automatic unit-friendly snap intervals or physical preservation
- Persistent Geometry Settings for automatic/manual grids, minor divisions, visibility, and snap tolerance
- Synchronized XY, XZ, and YZ wire views with live coordinates, engineering grids, fit, zoom, pan, and selection
- Interactive software-rendered 3D model view with orbit, pan, zoom, fit, axes, picking, and synchronized selection
- Structured FR frequency sweeps and EX voltage sources with validation, undo/redo, and 2D/3D feed markers
- Selectable EX markers with contextual properties, deletion, and nearest-segment source creation from 2D/3D wires
- Dedicated EX Properties dialog plus source and feed-reference management under Model → Sources
- Structured GN/GE ground environments: free space, perfect, real/fast, real/Sommerfeld, and average-ground preset
- Persistent Analysis backend selection with an independent executable path for nec2c, OpenNEC, and 4nec2 NEC2dXS
- Managed XQ/RP result requests with far-field angular grids and live analysis-readiness validation
- Asynchronous `nec2c` execution with timeout/cancel controls, run history, live output, and preserved artifacts
- Persistent selectable run history that reloads historical raw output, tables, and plots
- Persistent open-model indicator plus model filenames, timestamps, and backends across history and results
- Structured feedpoint results with frequency, complex impedance, phase, power, and model-referenced SWR tables
- Engineering-style sweep plots with logarithmic SWR, logarithmic resistance, independent linear reactance, and raw-value hover readouts
- Exhaustive one-variable sweeps plus bounded multi-variable adaptive, Nelder–Mead, and seeded Differential Evolution optimization with weighted impedance, forward-gain, and physical front-to-back objectives and undoable Apply Best
- Parsed per-segment currents with selectable-frequency distribution plots and tables
- Fully labeled, hover-tracked 2D gain cuts and layered 3D antenna, segment-current, and radiation results
- Previewable wavelength-based automatic segmentation with undo and safe EX source remapping
- Structured editable `LD` load and `TL` transmission-line tables with validation and undo
- Plane-aware endpoint and whole-wire dragging with grid/endpoint snapping, synchronized properties,
  `GW` source updates, and undo/redo
- Right-click wire properties for endpoints, segments, radius, and standard 4/0–40 AWG sizing
- Quick AWG radius selection from the contextual Properties dock
- Right-click wire creation, splitting, deletion, and property access with undoable source-deck edits
- Dependency-free core tests

## Documentation

- [User Guide](docs/user-guide.md) — workspace paths, editing, analysis, results, and interaction controls
- [Optimization Mathematics](docs/optimization-math.md) — objective equations and implemented search algorithms
- [Architecture](docs/architecture.md) — internal boundaries and current development milestones
- [Roadmap](docs/roadmap.md) — parameterization, optimization, and planned improvements
- [NEC Card Support](docs/nec-card-support.md) — complete NEC-2 coverage matrix and support levels

## Build

Requirements:

- CMake 3.21 or newer
- A C++20 compiler
- Qt 6.4 or newer with the Widgets module

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To build only the core and tests on a machine without Qt:

```sh
cmake -S . -B build -DNECWB_BUILD_GUI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

## Build Downloads

Every push to `main` runs Windows and Linux packaging workflows on GitHub. Open
the repository's **Actions** page and download an artifact from a successful run:

- `NEC-Workbench-Windows-x64`: extract the ZIP and run `nec-workbench.exe`.
- `NEC-Workbench-Linux-x86_64`: extract the ZIP, make the AppImage executable,
  and run it.

The packages include the required Qt runtime files but do not include an NEC
solver. Select a separately installed compatible solver executable in
**Analysis → Solver** when calculation support is needed.

## Architecture

`necwb_core` deliberately has no Qt dependency. NEC source is represented by
`NecDocument`; semantic antenna geometry is represented separately by
`AntennaModel`, including read-only expanded paths for arcs, helices, and tapered
wires. The solver command adapter and asynchronous Qt process runner
keep backend-specific invocation separate from the model and results layers.

Use **Tools → Check Model** or press `F7` to validate the open source deck.

See `docs/architecture.md` for the current boundaries and next milestones.
