# NEC Workbench User Guide

NEC Workbench organizes antenna modeling into six workspaces across the top of
the main window: **Home**, **Geometry**, **NEC Source**, **Analysis**,
**Results**, and **Optimize**. These are work areas rather than a required
step-by-step wizard. You can move between them whenever a model is open.

## Getting Started

### Create a model

Path: **Home → New NEC Model** or **File → New NEC Model** (`Ctrl+N`)

This creates a minimal NEC deck. Add or edit geometry in Geometry or NEC Source,
then configure frequency, ground, excitation, and requested output in Analysis.

### Open a model

Path: **Home → Open Existing** or **File → Open NEC File** (`Ctrl+O`)

The Home page also lists recent NEC files. Opening a model enables Geometry,
NEC Source, Analysis, Results, and—after successful validation—Optimize.

### Check a model

Path: **Model → Check Model**, the toolbar **Check Model** button, or `F7`

Check Model parses the authoritative NEC source, updates every graphical and
structured view, and reports errors and warnings in the Diagnostics dock.
Analysis is disabled when blocking model errors are present.

## Home

### Summary

Home is the starting point and model dashboard. With no model loaded, it offers
new/open actions, recent files, examples, and documentation entry points. With a
model loaded, it summarizes the active model, validation state, backend, and
most recent results.

### Common paths

- Start a model: **Home → New NEC Model**
- Open a model: **Home → Open Existing**
- Reopen recent work: **Home → Recent Models**
- Continue modeling: use the Geometry or NEC Source quick action
- Configure or run analysis: use the Analysis quick action
- Inspect available results: use the Results quick action

Home does not own a separate copy of the model. Its source preview and summary
reflect the same document used by every other workspace.

## Geometry

### Summary

Geometry provides synchronized graphical views of the checked antenna model.
Changes made here rewrite the corresponding NEC source cards and participate in
Undo/Redo.

Path: **Geometry** on the top workspace bar

### 2D Geometry

Path: **Geometry → 2D Geometry**

The XY, XZ, and YZ planes show the same wires from three orthographic views.

- Click a wire to select it and populate the Properties dock.
- Drag a wire endpoint to reshape the wire in the active plane.
- Drag the wire body to move the complete wire in the active plane.
- Use the mouse wheel to zoom and drag the background to pan.
- Use **Fit Geometry** to bring the full model back into view.
- Toggle **Snap Grid** and **Snap Endpoints** from the Geometry toolbar.
- Change display units from the toolbar without changing NEC's internal meter values.
- Move the pointer over a view to see live plane coordinates.

Right-click a wire to access:

- **Properties** — tag, segment count, endpoints, radius, and AWG size
- **Add Voltage Source Here**
- **Add Load Here**
- **Start/Finish Transmission Line Here**
- **Split Wire Here**
- **Delete Wire**

Right-click empty space to add a wire. Right-click an EX, LD, or TL marker to
open its properties or dedicated Analysis editor, or to delete it.

### 3D Geometry

Path: **Geometry → 3D Geometry**

- Left-drag to orbit.
- Shift-drag or middle-drag to pan.
- Use the mouse wheel to zoom.
- Use **Fit** to frame the model.
- Use **Isometric** to restore the standard 3D orientation.
- Click wires and attached markers to synchronize selection and Properties.
- Right-click wires and markers for the same source/load actions available in 2D.

The 3D geometry view currently supports selection and contextual editing; direct
3D endpoint dragging is not yet implemented.

### Geometry settings

Path: **Model → Geometry Settings**

Geometry Settings controls grid visibility, automatic or manual major spacing,
minor divisions, axes, labels, snapping, endpoint tolerance, and how snap values
behave when display units change.

### Automatic segmentation

Path: **Model → Automatic Segmentation**

Automatic Segmentation previews wavelength-based segment counts before changing
the model. You can choose segments per wavelength and request odd counts for
excited wires. Supported voltage sources are remapped to the nearest equivalent
normalized position. Apply the preview only after reviewing the proposed changes.

## NEC Source

### Summary

NEC Source is the authoritative representation of the model. Graphical and
structured editors always map their changes back to this text.

Path: **NEC Source** on the top workspace bar

### Raw NEC source

Path: **NEC Source → NEC Source**

Use the raw editor for direct NEC card editing, comments, unsupported cards, and
complete deck review. Line numbers, syntax highlighting, and diagnostics help
locate card errors. Moving the cursor updates the Properties dock with the
selected card and its fields.

After direct text edits, run **Check Model** before analysis.

### Structured cards

Path: **NEC Source → Structured Cards**

The structured editor provides cell-based editing for supported card families:

- **Wires (GW)** — tag, segments, endpoints, and radius
- **EX** — voltage and other excitation fields
- **FR** — single frequency and sweep fields
- **GN/GE** — ground environment and geometry ground flag
- **LD** — loads
- **TL** — transmission lines
- **RP** — radiation requests
- **XQ** — execution requests

Double-click a cell to edit it. Invalid fields are highlighted and are not
written back to the source. Add and Delete actions operate on the selected card
family and use the shared Undo/Redo history.

Unsupported cards remain preserved in raw source rather than being forced into
an unsafe generic editor.

### Project tree navigation

The Project dock lists cards under Geometry, Environment, Frequency & Sources,
Loads & Networks, Requests & Execution, Comments, Other Cards, and All Cards.

- Single-click any card to inspect it in Properties.
- Double-click `GW` to open the structured wire editor.
- Double-click `FR`, `GN`, `GE`, or `EX` to open Model Setup.
- Double-click `LD` or `TL` to open Loads & Lines.
- Double-click `RP` or `XQ` to open Analysis Requests.
- Double-click supported remaining cards to open their structured table.
- Double-click unsupported cards or comments to jump to their raw source line.

## Analysis

### Summary

Analysis defines how the active model will be solved. It combines model-level
electrical setup, loads and networks, external solver configuration, and result
requests.

Path: **Analysis** on the top workspace bar

### Model Setup

Path: **Analysis → Model Setup**

Configure:

- A single frequency or an `FR` frequency sweep
- Linear or multiplicative sweep spacing
- Free space, perfect ground, reflection-approximation ground, or
  Sommerfeld/Norton ground
- Relative permittivity and conductivity for real ground
- One or more voltage sources and their wire segment, magnitude, and phase

The setup controls edit the corresponding `FR`, `GN`, `GE`, and `EX` cards.

### Loads & Lines

Path: **Analysis → Loads & Lines**

Edit supported `LD` loads and `TL` transmission lines in structured tables.
Selections synchronize with their markers in Geometry and with the Properties
dock. Invalid wire or segment references are rejected before source changes are
applied.

### Solver

Path: **Analysis → Solver**

Select the backend family, executable, and timeout. `nec2c` is the first fully
supported process adapter. Solver executables are external and are not bundled
with NEC Workbench. Other backend choices remain visible for future adapters.

### Requests

Path: **Analysis → Requests**

Choose the data the solver should produce:

- Structure currents and feed impedance (`XQ`)
- Far-field radiation (`RP`)
- Theta and phi sampling ranges
- 2D elevation-cut or full-3D pattern presets
- Which frequencies in a sweep receive radiation calculations

The readiness panel explains anything still blocking a run.

### Run analysis

Path: **Analysis → Run Analysis**, the main toolbar, or the Analysis menu

NEC Workbench checks the model again, writes an archived `model.nec`, launches
the selected solver asynchronously, and preserves `model.nec`, `model.out`,
`run.log`, and JSON metadata in a unique run directory. Active runs can be
canceled and are subject to the configured timeout.

## Results

### Summary

Results displays current or historical solver output. Results and Run History
are available even when no model is currently open.

Path: **Results** on the top workspace bar

### Run History

Path: **Results → Run History**

Run History lists the originating model, start time, available result types,
output size, backend, status, duration, and artifact directory. Only one run can
be selected at a time.

- Click **Open Results** or double-click a run to load it.
- Click **Open Run Folder** to inspect archived files.
- Click **Delete Run** to permanently remove the selected run directory.
- Use **Cancel Active Run** while a solver process is running.

Opening historical results also loads the archived `model.nec` as a **historical
run snapshot**. Geometry, NEC Source, and Analysis then describe the exact deck
used for that run. The snapshot does not overwrite the original model; use
**File → Save As** to turn it into a normal editable file.

Missing or invalid archived files are reported in the Results status area. A
missing model never causes an unrelated antenna to be displayed with historical
radiation data.

### Result frequency

The Result Frequency control above the result tabs selects the active frequency
across compatible tables and plots. The availability summary indicates whether
impedance, currents, and radiation data exist at that frequency.

### Numerical Results

Path: **Results → Numerical Results**

Shows feedpoint frequency, source location, resistance, reactance, impedance
magnitude and phase, input power, and 50-ohm SWR.

### Sweep Plots

Path: **Results → Sweep Plots**

Shows resistance/reactance and SWR over frequency. Move the pointer over a plot
for exact values.

### Currents

Path: **Results → Currents**

Shows per-segment current magnitude and a table of complex current values for
the selected frequency.

### Radiation 2D

Path: **Results → Radiation 2D**

Select polarization component, scale, cut orientation, and cut angle. Move the
pointer over the plot for live angle, absolute dBi, and relative dB.

- Left/Right changes the available cut angle.
- Space switches horizontal and vertical cut orientation.
- **Show Peak Cut** moves to the sampled cut containing maximum gain.

### 3D Results

Path: **Results → 3D Results**

The 3D renderer can layer the archived antenna, current distribution, and
radiation surface. Orbit, pan, and zoom use the same controls as 3D Geometry.
Layers can be hidden independently.

### Raw NEC Output

Path: **Results → Raw NEC Output**

Displays the archived solver `model.out` for auditing or diagnosing output that
is not yet parsed into a structured result view. Process messages remain in the
Solver Output dock.

## Optimize

### Summary

Path: **Optimize** on the top workspace bar

Optimize provides a basic bounded sweep for one `SY` variable. Choose the
variable, minimum, maximum, number of candidate points, and reference impedance,
then select **Run SWR Sweep**. Workbench runs each candidate through the selected
NEC backend and identifies the value with the lowest worst-case SWR across all
frequencies in the model.

Radiation requests are omitted from these initial impedance-only candidates to
keep the sweep fast. Each candidate remains available in Results → Run History
and contains `model.source.nec`, the generated numeric `model.nec`, solver output,
and `optimization.json` metadata. The best value is reported but is not yet
automatically written back into the model.

Native NEC-2 solvers receive numeric cards. NEC Workbench will resolve symbols
before invoking the backend so parameterization remains solver-independent.

### Parameterized Source

Use `SY` cards in the raw NEC source to define reusable values and expressions:

```text
SY frequency=7.1, halfLength=10.03
SY segments=41, feedSegment=(segments+1)/2
GW 1 segments -halfLength 0 10 halfLength 0 10 0.001
FR 0 1 0 0 frequency 0
```

Names are case-insensitive and must be declared before use. Expressions support
parentheses and `+`, `-`, `*`, `/`, and `^`. **Check Model** reports expression
errors on the corresponding source line. When a run starts, Workbench saves the
authored deck as `model.source.nec` and sends a generated numeric `model.nec` to
the solver. See `examples/40m-symbolic-dipole.nec` for a complete model.

For this initial checkpoint, edit symbolic fields in the raw source editor.
Structured or geometry edits can replace an expression with its resolved numeric
value.

## Global Docks and Controls

### Project

Shows the active model and categorized NEC cards. Single-click updates
Properties; double-click navigates to the most appropriate editor.

### Properties

Shows contextual data for the current wire, source, load, transmission line, or
NEC card. Supported wire selections include direct radius and AWG controls.

### Diagnostics

Shows model-check errors and warnings. Click a diagnostic to jump to its source
line.

### Solver Output

Shows the command, process messages, standard output, and standard error from
the active or selected run.

### Undo and Redo

Path: **Edit → Undo/Redo** or the platform's standard Undo/Redo shortcuts

Geometry and structured edits share source-level Undo/Redo. Direct edits in the
raw source editor use the text editor's Undo/Redo history.

## Typical Workflow

1. Create or open a NEC model from Home.
2. Build or inspect wires in Geometry or NEC Source.
3. Configure frequency, ground, and excitation in Analysis → Model Setup.
4. Add loads or transmission lines if needed.
5. Configure the solver executable.
6. Choose current/impedance and radiation requests.
7. Run Check Model and resolve blocking diagnostics.
8. Run Analysis.
9. Inspect numerical, sweep, current, and radiation results.
10. Reopen the preserved run later from Results → Run History.
