# NEC Workbench

NEC Workbench is a cross-platform Qt 6 desktop application for building,
checking, running, and optimizing NEC antenna models. It comes with the `nec2c`
solver, run as a separate program, and can use your own OpenNEC, 4nec2 NEC2dXS, or
`nec2c` instead.

## Highlights

- Raw and structured NEC editing over one authoritative source document
- One resizable main window with a model tree, variable sliders, and properties pane
  beside the geometry editor (synchronized 2D/3D wire editing, selection, units, and
  snapping), with the NEC Deck (raw source) shown in its place
- Structured sources, grounds, loads, transmission lines, frequencies, and requests
- External-solver execution with live status, durable run artifacts, run notes, and history
- SWR, impedance, and interactive 2D/3D radiation results
- Currents drawn on the antenna as magnitude, phase relative to the feed, or an
  animated RF cycle, plus along-wire plots and a segment table
- One main window: toolbar, model tree, geometry editor, and a results column with key
  numbers and live thumbnails that open full views in the center
- An optional labeled historical overlay for impedance, SWR, and radiation results
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

## Examples

The [examples](examples) folder contains ready-to-run decks, also shipped in both packages
(**Open Example…** starts there), including:

- `2m-3-element-yagi.nec` — free-space Yagi with a direct 50 Ω feed; directional
  results, F/B optimization, and element current phase
- `40m-vertical-elevated-radials.nec` — quarter-wave vertical over average real ground
  with drooping radials; takeoff angle and currents at a multi-wire junction
- `40m-phased-vertical-array.nec` — two verticals fed in quadrature over perfect
  ground; multiple sources, a cardioid pattern, and the animated currents view
- `40m-symbolic-dipole.nec` — `SY` variables and expressions
- `80-6 ocfd.nec`, the folded dipoles, and the `geometry-*` decks — off-center feeds,
  transmission lines, and generated `GM`/`GR`/`GX` geometry

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

## Install

Download the package for your system from the repository's **Releases** page. The
current release is `0.1.0-alpha.3`. There is nothing else to install: each package
contains the Qt runtime, `nec2c` 1.3.3 built from the pinned commit in
`packaging/nec2c` with its license notices in `THIRD_PARTY_LICENSES.txt`, and the
examples. Each release also carries the corresponding nec2c source.

Package names carry the version, such as `NEC-Workbench-0.1.0-alpha.3-windows-x64.zip`
and `NEC-Workbench-0.1.0-alpha.3-x86_64.AppImage`.

- **Windows:** extract `NEC-Workbench-<version>-windows-x64.zip` and run `nec-workbench.exe`. Keep
  the extracted files together: the DLLs and folders beside the executable are needed,
  nec2c is in `solvers/`, and the examples are in `examples/`.
- **Linux:** make `NEC-Workbench-<version>-x86_64.AppImage` executable and run it:

  ```sh
  chmod +x NEC-Workbench-0.1.0-alpha.3-x86_64.AppImage
  ./NEC-Workbench-0.1.0-alpha.3-x86_64.AppImage
  ```

  Without FUSE, run it with `--appimage-extract-and-run`.

To use another engine, choose **Configure…** in the engine list beside Run.

## Build Artifacts

Packaging workflows run for `main`, `release/**` and other selected branches, pull
requests, and manual dispatch. Download artifacts from a successful run on the
repository's **Actions** page: `NEC-Workbench-Linux-x86_64` and
`NEC-Workbench-Windows-x64`, each with the package and the nec2c source archive.

Pushing a `v<version>` tag that matches the CMake version builds both packages from the
tag, runs their package checks, and publishes the GitHub release with both packages and
the nec2c source archive, using `docs/release-notes/<tag>.md` as its notes. Nothing is
published if a build or check fails.

## Design Boundary

`necwb_core` has no Qt dependency. Parsing, source preservation, semantic model
conversion, solver input generation, result parsing, validation, and optimization
logic remain separate from the Qt Widgets interface so they can be tested and
reused independently.

## License

NEC Workbench is free software licensed under the
[GNU General Public License version 3 or later](LICENSE).
