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
structured view, and reports errors and warnings in the Model Adequacy dock.
Analysis is disabled when blocking model errors are present. Adequacy warnings
do not block a run; they identify numerical-modeling conditions that should be
reviewed before trusting or optimizing the results.

The first adequacy checks evaluate every wire at the highest modeled frequency.
They report segments longer than `0.1` wavelength or shorter than `0.001`
wavelength, segment-length/diameter ratios at or below `4`, center feeds on
even-segment wires, adjoining segment lengths differing by more than `2:1`, and
junctions containing more than 30 wires. These are conservative NEC-2 guidance,
not proof that a model is correct or incorrect.

Average Gain Test and convergence status are not inferred from these static
checks. **Home → Model Quality** provides separate solver-backed Average Gain
Test and Segmentation Convergence studies.

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
- Run model-quality validation: **Home → Model Quality → Run Average Gain Test**

Home does not own a separate copy of the model. Its source preview and summary
reflect the same document used by every other workspace.

The **3D Overview** is a clean interactive preview of the latest antenna and
radiation result. Drag to orbit, use Shift-drag or the middle mouse button to
pan, and use the wheel to zoom. Detailed layer, scale, export, and component
controls remain in **Results → Radiation → 3D Pattern**.

### Average Gain Test

Path: **Home → Model Quality → Run Average Gain Test** or
**Model → Run Average Gain Test**

Choose one frequency and either free space or perfect ground. Workbench resolves
symbols, creates a temporary lossless solver deck, replaces finite ground with
the selected ideal test environment, and requests whole-space or hemisphere
gain averaging. It preserves the authored source as `model.source.nec` and does
not modify the open model.

The result appears under **Results → Validation** and in Runs as type **AGT**.
Free-space results are normalized to an expected average power gain of `1.0`;
perfect-ground results are normalized to `2.0`. The displayed gain adjustment is
diagnostic guidance, not a substitute for correcting a questionable model.

### Segmentation Convergence

Path: **Home → Model Quality → Segmentation Convergence**, **Model →
Segmentation Convergence**, or **Results → Validation → Segmentation
Convergence**

Choose one frequency, a percentage increase, at least three segmentation
levels, and comparison tolerances. Workbench repeatedly runs the same resolved
model, remapping supported voltage sources, loads, and transmission-line
endpoints by relative physical position. The authored source is unchanged.

The table compares resistance, reactance, complex-impedance change, and—when the
model requests radiation—sampled peak-gain change. A level is within tolerance only
relative to the preceding level. The overall study reports stability after two
consecutive refinements meet the selected limits. `NT` cards and unsupported EX
types currently block the study because their references cannot yet be remapped
safely.

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
excited wires. Supported `EX 0` sources, `LD` segment ranges, and both endpoints
of `TL` cards are remapped to the nearest equivalent normalized wire position.
TL-connected wires count as excited for the odd-center policy. Apply the preview
only after reviewing the proposed changes. `NT` network cards and unsupported
EX, LD, or TL forms still block automatic segmentation.

Automatic Segmentation edits only segment-count and segment-reference fields.
Symbolic `GW` coordinates and radii, plus unrelated symbolic EX, LD, and TL
values, remain unchanged in the authored source.

## NEC Source

### Summary

NEC Source is the authoritative representation of the model. Graphical and
structured editors always map their changes back to this text.

Graphical movement, splitting, and wire-property replacement are blocked when a
`GW` uses symbolic coordinates or radius. Edit its `SY` definitions or raw source
instead; Workbench does not silently replace those expressions with numbers.

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

The **NEC deck geometry units** selector is independent of Geometry display
units. It identifies the units actually written in geometry cards and manages a
standard `GS` conversion before `GE`: meters use no scale card, feet use
`GS 0 0 0.3048`, inches use `0.0254`, centimeters use `0.01`, and millimeters
use `0.001`. Changing this selector on a numeric `GW` deck rewrites coordinates
and radii while preserving the antenna's physical dimensions. Decks with
symbolic `GW` expressions or unsupported geometry generators are recognized but
are not automatically rewritten; update their expressions and `GS` together.

The structured editor provides cell-based editing for supported card families:

- **Wires (GW)** — tag, segments, endpoints, and radius
- **GS** — geometry-to-meter scale factor
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
- Double-click `LD` or `TL` to open **Model Setup → Loads & Transmission Lines**.
- Double-click `RP` or `XQ` to open Analysis Requests.
- Double-click supported remaining cards to open their structured table.
- Double-click unsupported cards or comments to jump to their raw source line.

## Analysis

### Summary

Analysis defines how the active model will be solved. It combines model-level
electrical setup, loads and networks, external solver configuration, and result
requests. Its three top-level tabs are Model Setup, Solver, and Requests.

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
Frequency and Ground share an adjustable two-column row, while the voltage-source
table remains full width. Use the nested **Frequency, Ground & Sources** and
**Loads & Transmission Lines** tabs to switch between compact forms and the
wider attachment tables.

Linear sweeps include the starting point and every complete step through the
requested end. For example, 14.000–14.350 MHz in 0.010 MHz steps produces 36
frequencies. NEC stores that number in the second `FR` integer field; the end
frequency itself is derived rather than written directly on the card.

### Loads & Lines

Path: **Analysis → Model Setup → Loads & Transmission Lines**

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

Center-only and representative radiation modes limit the expensive `RP`
calculations only. The complete `FR` sweep is still executed for feedpoint
impedance and SWR, even when the authored model has no explicit `XQ` card.

The readiness panel explains anything still blocking a run.

### Run analysis

Path: **Analysis → Run Analysis**, the main toolbar, or the Analysis menu

NEC Workbench checks the model again, writes an archived `model.nec`, launches
the selected solver asynchronously, and preserves `model.nec`, `model.out`,
`run.log`, and JSON metadata in a unique run directory. Active runs can be
canceled and are subject to the configured timeout. While a run is active, the
status bar shows an indeterminate activity indicator, the current phase, elapsed
time, growing output-file size, and a Cancel button. The Solver Output dock
continues to show process messages as they become available.

Solver and Requests pages scroll when the window is smaller than their usable
content. Input controls retain their normal text height rather than collapsing;
reduce the content area or use the page scroll bars to reach additional fields.

## Results

### Summary

Results displays current or historical solver output. Results and Run History
are available even when no model is currently open.

Path: **Results** on the top workspace bar

### Detachable Results Window

Use **Detach Results** in the Results banner or **View → Detach Results Window**
to move the complete Results workspace into one reusable top-level window. The
main window returns to the previous modeling workspace, so Geometry or NEC Source
can remain visible while plots update. Selecting Results while detached raises
that window instead of replacing the main workspace.

The detached window can be resized, minimized, maximized, or tiled normally and
remembers its last geometry. Completing or opening another run updates that same
window without changing its size or position. Choose **Attach to Main Window**, use the placeholder
in the main Results page, or close the detached window to return the same Results
workspace to the main application. Detaching does not create another result copy
or another window per run.

A successful standard analysis automatically detaches or raises this Results
window. The selected Results tab and its nested tab—such as **Radiation → 3D
Pattern** or **Impedance → Plots**—remain selected when the new result replaces
the previous one. Failed and canceled runs do not open the window automatically.

The **Impedance**, **Currents**, and **Radiation** pages also provide **Pop Out**.
Each opens one reusable live category window, so selected plots can remain beside
Geometry while subsequent analyses update them in place. Closing a category
window or choosing **Attach Here** returns the same widget to Results.

### Runs

Path: **Results → Runs**

Runs lists the originating model, run type, start time, available result types,
output size, backend, status, duration, and artifact directory. Only one run can
be selected at a time.

Ordinary analyses appear as individual rows. An optimization sweep appears as
one **Optimization** session row; its candidate runs remain stored as children
but do not flood the main list. View the session to restore its candidate table
in Optimize as an archived, read-only session. A segmentation study similarly appears as one **Convergence** row
with hidden child levels. Deleting either session deletes its child runs as a
group.

- Click **View Run Results** or double-click a run to display archived results without changing the active model.
- Click **Inspect Input Snapshot** to view the run's immutable `model.nec` deck in a read-only window.
- Click **Open Snapshot as New Model** only when you intentionally want the archived deck to replace the editor as an untitled editable copy.
- Click **Open Run Folder** to inspect archived files.
- Click **Delete Run** to permanently remove the selected run directory.
- Use **Cancel Active Run** while a solver process is running.

Viewing an ordinary historical analysis leaves Geometry, NEC Source, and Analysis attached
to the active model. Results loads the archived `model.nec` internally only for historical
geometry and radiation overlays. A persistent banner identifies the viewed run, states that
it is archived output, and names the active model separately. When results from the active
model are available, **Return to Current Work** restores them without changing the editor.

Every historical view provides **Return to Current Work**. It returns to the
workspace that was active before history was opened, or to the active model's
results when those were being viewed. Historical optimization and convergence
sessions disable their setup and Run controls; archived sessions cannot be
rerun in place or silently use the currently edited model.

AGT and convergence sessions reopen their Validation results without loading a
temporary transformed test deck as the editable model.

### Validation

Path: **Results → Validation**

The **Average Gain Test** page shows the normalized lossless-model test and
includes **Run Average Gain Test…**, which launches the same checked workflow as
the Model menu and Home dashboard. The
**Segmentation Convergence** page configures, runs, and restores mesh-refinement
studies. These are model-adequacy tools, not optimization algorithms.

Missing or invalid archived files are reported in the Results status area. A
missing model never causes an unrelated antenna to be displayed with historical
radiation data.

### Result frequency

The Result Frequency control above the result tabs selects the active frequency
across compatible tables and plots. The availability summary indicates whether
impedance, currents, and radiation data exist at that frequency.

### Result Summary

Path: **Results → Summary**

Shows the loaded run identity, frequency coverage, selected-frequency impedance,
minimum and maximum SWR, selected-frequency peak radiation, and available data
families. Summary becomes the active page after a successful run is loaded.

### Impedance

Path: **Results → Impedance**

The **Table** page shows feedpoint frequency, source location, resistance,
reactance, impedance magnitude and phase, input power, and 50-ohm SWR. The
**Plots** page shows resistance/reactance and SWR over frequency, with exact
pointer values. Y axes use automatically selected whole-number intervals, and a
separate label gutter keeps the impedance and SWR axis titles clear of tick values.

### Currents

Path: **Results → Currents**

Shows per-segment current magnitude and a table of complex current values for
the selected frequency.

### Radiation

Path: **Results → Radiation**

The **2D Pattern** page provides polarization component, scale, cut orientation,
and cut angle controls. Move the pointer over the plot for live angle, absolute
dBi, and relative dB.

- Left/Right changes the available cut angle.
- Space switches horizontal and vertical cut orientation.
- **Show Peak Cut** moves to the sampled cut containing maximum gain.

The **3D Pattern** page can layer the archived antenna, current distribution,
and radiation surface. Orbit, pan, and zoom use the same controls as 3D Geometry.
Layers can be hidden independently.

### Raw Output

Path: **Results → Raw Output**

Displays the complete, unchanged solver `model.out` for auditing or diagnosing
output that is not yet parsed into a structured result view. Changing Result
Frequency does not rewrite this authoritative artifact. Use **Jump to Selected
Frequency** to position the editor at the matching solver section, or **Find
Next** for free-text search. Process messages remain in the Solver Output dock.

## Optimize

### Summary

Path: **Optimize** on the top workspace bar

Optimize provides a basic bounded sweep for one `SY` variable. Choose the
variable, minimum, maximum, number of candidate points, reference impedance, and
SWR objective, then select **Run Parameter Sweep**.

The compact setup band keeps **Symbols & Expressions** beside **Sweep &
Frequencies**. Sweep settings use an interactive grid whose value cells contain
the appropriate dropdown or numeric control. The divider can be dragged
horizontally, while the candidate and per-frequency result tables retain most of
the workspace below. Decimal values are displayed and entered to three places
throughout the optimizer; archived raw solver output remains unchanged.

**Frequency Source** controls the frequencies calculated for every candidate:

- **Use Model FR Sweep** keeps the model's existing `FR` definition.
- **Use Selected Frequencies** evaluates only the explicit MHz values in the
  editable list. Add values individually or paste a list separated by spaces,
  commas, semicolons, or new lines. This supports separated bands without
  calculating every frequency between them.

The workload summary shows candidate count × frequency count before the sweep.

Symbols used directly in `GW` coordinate or radius fields are labeled with the
detected NEC deck geometry unit. Candidate values remain source values; the
deck's `GS` card performs the conversion to meters. Frequency and unitless
symbols do not receive a geometry-unit suffix.

This tool exhaustively evaluates the requested candidate points. “Complete”
means every candidate was attempted; it is not optimizer convergence. A future
adaptive optimizer will separately report search stopping criteria. Future
tolerance analysis will perturb a finalist to measure construction and component
sensitivity.

**Minimize Worst SWR Across Frequencies** scores each candidate using its highest
calculated SWR. This is the appropriate choice when every modeled or explicitly
selected frequency must remain usable. **Minimize SWR at Selected Frequency** scores the
calculated frequency nearest the requested frequency. If the model contains only
one frequency, both objectives produce the same score.

Radiation requests are omitted from these initial impedance-only candidates to
keep the sweep fast. Each candidate remains available in Results → Run History
and contains `model.source.nec`, the generated numeric `model.nec`, solver output,
and `optimization.json` metadata. The results table identifies the frequency and
feedpoint impedance that produced each score. Select a completed candidate to
inspect its SWR, resistance, and reactance at every calculated frequency; the
frequency that determines the objective is bold. The best value is reported but is not yet
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

### Numeric Display

Workbench values use three digits after the decimal throughout Geometry, Analyze,
Results, Optimize, setup panels, properties, and dialogs. Very small or very
large values use scientific notation with three decimal places so meaningful
wire and component values do not appear as zero. Authored NEC source, raw solver
output, and exported result data retain their original precision. Precision-sensitive
radius and conductivity editors also retain the additional entry precision needed
to represent their values safely.

### Project

Shows the active model and categorized NEC cards. Single-click updates
Properties; double-click navigates to the most appropriate editor.

### Properties

Shows contextual data for the current wire, source, load, transmission line, or
NEC card. Supported wire selections include direct radius and AWG controls.

### Model Adequacy

Shows source/card errors, readiness warnings, compatibility notices, and static
model-adequacy findings in separate categories. Click a finding to jump to its
source line. A clean static report does not replace **Results → Validation →
Average Gain Test** or segmentation convergence testing.

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
