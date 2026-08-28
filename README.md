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
- A dockable engineering workbench shell with project, properties, diagnostics, and solver-output panels
- Dashboard, Geometry, NEC Source, Analysis, Results, and Optimize workspaces in one main window
- A live dashboard combining the shared NEC document, interactive 3D renderer, model summary, and quick results
- An editable `GW` wire-card table synchronized with source, geometry, project selection, and undo/redo
- Selectable metric/imperial display units with automatic unit-friendly snap intervals or physical preservation
- Persistent Geometry Settings for automatic/manual grids, minor divisions, visibility, and snap tolerance
- Synchronized XY, XZ, and YZ wire views with live coordinates, engineering grids, fit, zoom, pan, and selection
- Interactive software-rendered 3D model view with orbit, pan, zoom, fit, axes, picking, and synchronized selection
- Structured FR frequency sweeps and EX voltage sources with validation, undo/redo, and 2D/3D feed markers
- Selectable EX markers with contextual properties, deletion, and nearest-segment source creation from 2D/3D wires
- Dedicated EX Properties dialog plus batch source management in Model Setup
- Structured GN/GE ground environments: free space, perfect, real/fast, real/Sommerfeld, and average-ground preset
- Persistent Analyze backend selection for NEC-2, OpenNEC, NEC-4-compatible, or custom executables
- Managed XQ/RP result requests with far-field angular grids and live analysis-readiness validation
- Asynchronous `nec2c` execution with timeout/cancel controls, run history, live output, and preserved artifacts
- Persistent selectable run history that reloads historical raw output, tables, and plots
- Persistent open-model indicator plus model filenames and run IDs across history, logs, tables, and plots
- Structured feedpoint results with frequency, complex impedance, phase, power, and 50-ohm SWR tables
- Interactive resistance/reactance and 50-ohm SWR sweep plots with automatic scaling and hover values
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

## Architecture

`necwb_core` deliberately has no Qt dependency. NEC source is represented by
`NecDocument`; semantic antenna geometry is represented separately by
`AntennaModel`. The solver command adapter and asynchronous Qt process runner
keep backend-specific invocation separate from the model and results layers.

Use **Tools → Check Model** or press `F7` to validate the open source deck.

See `docs/architecture.md` for the current boundaries and next milestones.
