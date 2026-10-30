# NEC Workbench

NEC Workbench is a cross-platform Qt 6 desktop application for building,
checking, running, and optimizing NEC antenna models. Solver executables remain
external; Workbench currently supports `nec2c`, OpenNEC, and 4nec2 NEC2dXS
process protocols.

## Highlights

- Raw and structured NEC editing over one authoritative source document
- Separate Geometry Editor (synchronized 2D/3D wire editing, selection, units, and
  snapping) and NEC Deck (raw source, structured cards, and parameters) windows
- Structured sources, grounds, loads, transmission lines, frequencies, and requests
- External-solver execution with live status, durable run artifacts, run notes, and history
- SWR, impedance, and interactive 2D/3D radiation results
- Currents drawn on the antenna as magnitude, phase relative to the feed, or an
  animated RF cycle, plus along-wire plots and a segment table
- Compact Home control panel with quick results and reusable setup, result, and run windows
- Optional labeled historical overlays for impedance, SWR, and radiation results
- Static model checks, Average Gain Test, and segmentation convergence studies
- `SY` parameterization plus sweep, adaptive, Nelder–Mead, and Differential
  Evolution optimization with impedance and directional objectives
- Read-only semantic display for supported generated geometry and surface cards

NEC Workbench preserves unsupported or unknown source lines rather than silently
discarding them. See the [card support matrix](docs/nec-card-support.md) for the
difference between preserved, understood, structured, and graphically rendered
cards.

## Documentation

- [User Guide](docs/user-guide.md) — normal modeling, analysis, results, and optimization workflows
- [Architecture](docs/architecture.md) — source, model, solver, result, and UI boundaries
- [NEC Card Support](docs/nec-card-support.md) — support level by NEC mnemonic
- [NEC Validation Reference](docs/nec-validation-reference.md) — backend profiles, checks, and audit boundaries
- [Roadmap](docs/roadmap.md) — current priorities and longer-term direction

Most individual controls also provide tooltips in the application.

## Build

Requirements:

- CMake 3.21 or newer
- A C++20 compiler
- Qt 6.4 or newer with Widgets

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Without `-DCMAKE_BUILD_TYPE`, single-configuration builds default to `RelWithDebInfo`
(optimized, with debug symbols). Pass `-DCMAKE_BUILD_TYPE=Debug` for an unoptimized
build.

Core-only builds do not require Qt:

```sh
cmake -S . -B build -DNECWB_BUILD_GUI=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

## Releases

Packaged alpha releases are published on the repository's **Releases** page. The
current release is `0.1.0-alpha.2`.

## Build Artifacts

Packaging workflows run for `main`, selected integration branches, pull requests,
and manual dispatch. Download artifacts from a successful run on the repository's
**Actions** page:

- `NEC-Workbench-Linux-x86_64`
- `NEC-Workbench-Windows-x64`

Artifacts include the required Qt runtime files but not an NEC solver. Configure
a separately installed solver under **Setup → Solver**.

## Design Boundary

`necwb_core` has no Qt dependency. Parsing, source preservation, semantic model
conversion, solver input generation, result parsing, validation, and optimization
logic remain separate from the Qt Widgets interface so they can be tested and
reused independently.

## License

NEC Workbench is free software licensed under the
[GNU General Public License version 3 or later](LICENSE).
