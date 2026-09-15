# NEC Workbench User Guide

NEC Workbench organizes antenna modeling into five workspaces across the top of
the main window: **Home**, **Model**, **Analysis**, **Results**, and **Optimize**.
The Model workspace contains **Geometry**, **Parameters**, **Sources**, **Loads &
Transmission Lines**, **Environment**, and **NEC Deck**. These are work areas
rather than a required step-by-step wizard. You can move between them whenever a
model is open.

The desktop menus provide the complete command inventory:

- **File** contains New, Open, Save, Save As, and Exit.
- **Edit** contains Undo, Redo, and focus-aware Cut, Copy, and Paste commands.
- **View** controls the Project, Model Adequacy, and Solver Output docks, detached
  Results, and **Reset Layout**. Reset Layout reattaches Results and restores the
  default dock arrangement without changing the model.
- **Model** contains Check Model, Automatic Segmentation, Average Gain Test,
  Segmentation Convergence, and Geometry Settings.
- **Run** contains Run Analysis and a global Stop command for the active analysis,
  validation study, or parameter sweep.
- **Help** contains Getting Started, the local User Guide, and About.

## Getting Started

### Create a model

Path: **Home → New NEC Model** or **File → New NEC Model** (`Ctrl+N`)

This creates a minimal NEC deck. Build its physical and electrical definition
under Model, then configure frequency, solver, and requested output in Analysis.

### Open a model

Path: **Home → Open Existing** or **File → Open NEC File** (`Ctrl+O`)

The Home page also lists recent NEC files. Opening a model enables Model,
Analysis, Results, and—after successful validation—Optimize.

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
- Continue modeling: select **Model → Geometry** or **Model → NEC Deck**
- Configure analysis: select the **Analysis** workspace tab
- Inspect available results: select the **Results** workspace tab
- Check or run the active model: use the Home actions or main toolbar
- Run model-quality validation: **Home → Model Quality → Run Average Gain Test**

Home does not own a separate copy of the model. Its source preview and summary
reflect the same document used by every other workspace.

Workspace tabs are the primary navigation. Home therefore avoids duplicate
Model, Analysis, and Results destination buttons. Its heading
keeps project identity uncluttered; **Check Model** and **Run Analysis** share
the NEC Source panel's title row at the upper-right. AGT and convergence appear side by
side beneath Model Summary so the summary retains more vertical space. Automatic
Segmentation remains available from the **Model** menu rather than the main
toolbar.

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

## Model

### Summary

Model groups the synchronized geometry, parameters, sources, loads and transmission
lines, environment, and NEC-deck representations of the physical/electrical model.
These views share one authoritative source document and undo/redo history.

Path: **Model** on the top workspace bar

### Geometry

Geometry provides synchronized graphical views of the checked antenna model.
Changes made here rewrite the corresponding NEC source cards and participate in
Undo/Redo.

Path: **Model → Geometry**

#### 2D Geometry

Path: **Model → Geometry → 2D Geometry**

The XY, XZ, and YZ planes show the same wires from three orthographic views.

- Click a wire to synchronize selection across the geometry views and Project tree.
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
open its properties or dedicated Model editor, or to delete it.

#### 3D Geometry

Path: **Model → Geometry → 3D Geometry**

- Left-drag to orbit.
- Shift-drag or middle-drag to pan.
- Use the mouse wheel to zoom.
- Use **Fit** to frame the model.
- Use **Isometric** to restore the standard 3D orientation.
- Click wires and attached markers to synchronize selection across views and editors.
- Right-click wires and markers for the same source/load actions available in 2D.

The 3D geometry view currently supports selection and contextual editing; direct
3D endpoint dragging is not yet implemented.

#### Geometry settings

Path: **Model → Geometry Settings**

Geometry Settings controls grid visibility, automatic or manual major spacing,
minor divisions, axes, labels, snapping, endpoint tolerance, and how snap values
behave when display units change.

#### Automatic segmentation

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

### Parameters

Path: **Model → Parameters**

The Parameters editor lists each `SY` definition as **Name**, authored
**Expression**, and unit-neutral **Resolved Value**. Double-click a Name or
Expression cell to edit it; Resolved Value remains read-only. **Add Parameter**
creates a draft row even when the model has no existing `SY` cards. Apply or
revert the highlighted draft before editing another row. Multiple assignments on
one `SY` source line remain supported; changing or deleting one assignment
preserves the others. Applied changes update the authoritative NEC source through
shared Undo/Redo and are immediately available to Parameter Sweep.
An edit that introduces an expression error—or deletes a parameter still used by
the model—is rejected with an explanation instead of leaving a broken definition.

Parameter names are case-insensitive and must begin with a letter or underscore.
Workbench does not infer feet, meters, or another physical unit from a parameter
name or usage. Use explicit conversion expressions where needed.

### Sources

Path: **Model → Sources**

Edit standard voltage-source `EX 0` cards by wire, segment, magnitude, and phase.
Selections synchronize with source markers in the 2D/3D Geometry views and the
Project tree.

The **Feed System** section sets the model reference impedance used by SWR
tables, plots, dashboard summaries, and optimizer defaults. **Apply Reference
Impedance** writes the canonical `Z0` compatibility card. **Use Default 50 Ω**
removes that card and returns Workbench calculations to 50 ohms. This value is
reporting and matching metadata; it does not alter the NEC electromagnetic field
solution or a `TL` card's characteristic impedance.

### Loads & Networks

Path: **Model → Loads & Transmission Lines**

Edit supported `LD` loads and `TL` transmission lines in structured tables.
Selections synchronize with their Geometry markers, and invalid wire or segment
references are rejected before source changes are applied. The existing editor
behavior and NEC-card writing remain unchanged in this rollout.

Changes in the load and transmission-line tables are staged until the matching
**Apply Selected** button is pressed. The button is highlighted while edits are
pending. Leaving the page offers a choice to discard the edits or return and
apply them.

### Environment

Path: **Model → Environment**

Configure free space, perfect ground, reflection-approximation ground, or
Sommerfeld/Norton ground. Real-ground controls include material presets,
relative permittivity, conductivity, and the `GE` ground-connection flag.

The material list provides approximate single-medium starting values for salt
water, fresh water, very good, good, average, poor rocky, sandy/dry, and urban
ground. Selecting a preset fills relative permittivity and conductivity; editing
either value changes the selection to **Custom**. Prefer measured local values
when available. These presets do not define a two-medium `GN`/`GD` ground model.

Frequency, source, and environment forms use the same staged-edit behavior: the
relevant Apply or Update button is highlighted after a field changes, returns to
normal after a successful application, and protects unapplied values during
workspace navigation.

### NEC Deck

NEC Deck contains the raw and structured representations of the same
authoritative source document used by Geometry and every other workspace.

Path: **Model → NEC Deck**

Graphical movement, splitting, and wire-property replacement are blocked when a
`GW` uses symbolic coordinates or radius. Edit its `SY` definitions or raw source
instead; Workbench does not silently replace those expressions with numbers.

#### Raw NEC source

Path: **Model → NEC Deck → Raw Source**

Use the raw editor for direct NEC card editing, comments, unsupported cards, and
complete deck review. Line numbers, syntax highlighting, and diagnostics help
locate card errors.

After direct text edits, run **Check Model** before analysis.

#### Structured cards

Path: **Model → NEC Deck → Structured Cards**

The **NEC deck geometry units** selector is independent of Geometry display
units. It identifies the units actually written in geometry cards and manages a
standard `GS` conversion before `GE`: meters use no scale card, feet use
`GS 0 0 0.3048`, inches use `0.0254`, centimeters use `0.01`, and millimeters
use `0.001`. Changing this selector on a numeric `GW` deck rewrites coordinates
and radii while preserving the antenna's physical dimensions. Decks with
symbolic `GW` expressions or unsupported geometry generators are recognized but
are not automatically rewritten; update their expressions and `GS` together.

The structured editor groups supported card types under **Geometry**,
**Environment**, **Sources**, **Loads & Networks**, **Analysis & Requests**, and
**Program Control**. Selecting a leaf displays its card-specific columns:

- **Wires (GW)** — tag, segments, endpoints, and radius
- **GS** — geometry-to-meter scale factor
- **EX** — voltage and other excitation fields
- **FR** — single frequency and sweep fields
- **GN/GE** — ground environment and geometry ground flag
- **LD** — loads
- **TL** — transmission lines
- **RP** — radiation requests
- **XQ** — execution requests
- **Z0/ZO** — xnec2c-compatible SWR/reference impedance

Double-click a cell to edit it. Invalid fields are highlighted and are not
written back to the source. Add and Delete actions operate on the selected card
type and use the shared Undo/Redo history. The hierarchy changes navigation only;
it does not duplicate the friendly editors or underlying source model.

Unsupported cards remain preserved in raw source rather than being forced into
an unsafe generic editor.

`Z0` sets the reference impedance used by Workbench SWR displays and optimizer
defaults; `ZO` is accepted as the legacy spelling. The value must be a positive
number in ohms. These are xnec2c compatibility cards rather than standard NEC-2
cards, so Workbench preserves them in the authored source but omits them from
the temporary deck sent to `nec2c`. If neither card exists, Workbench uses
50 ohms.

The **Wires (GW)** table includes both the NEC radius field and a **Wire Gauge**
convenience selector. It offers every AWG size from 10 through 30, including odd
sizes. Selecting a gauge writes its nominal bare-conductor radius through the
normal wire-edit path; a manually entered nonstandard radius displays as
**Custom radius**. Gauge selection is disabled for symbolic GW geometry so an
`SY` radius expression is never silently replaced.

#### Project tree navigation

The Project dock lists cards under Geometry, Environment, Frequency & Sources,
Loads & Networks, Requests & Execution, Comments, Other Cards, and All Cards.

- Single-click wires, sources, loads, and transmission lines to synchronize their
  graphical and structured-editor selections.
- Double-click `GW` to open the structured wire editor.
- Double-click `FR` to open **Analysis → Frequency**.
- Double-click `GN` or `GE` to open **Model → Environment**.
- Double-click `EX` to open **Model → Sources**.
- Double-click `LD` or `TL` to open **Model → Loads & Transmission Lines**.
- Double-click `RP` or `XQ` to open Analysis Requests.
- Double-click supported remaining cards to open their structured table.
- Double-click unsupported cards or comments to jump to their raw source line.

## Analysis

### Summary

Analysis defines how the active model will be solved. Its tabs are Solver,
Frequency, and Requests; model-defining sources, loads, lines, and ground now
live under Model.

Path: **Analysis** on the top workspace bar

### Frequency

Path: **Analysis → Frequency**

Configure:

- A single frequency or an `FR` frequency sweep
- Linear or multiplicative sweep spacing

The frequency controls edit the corresponding `FR` card while Structured Cards
continues to expose its underlying NEC fields.

Linear sweeps include the starting point and every complete step through the
requested end. For example, 14.000–14.350 MHz in 0.010 MHz steps produces 36
frequencies. NEC stores that number in the second `FR` integer field; the end
frequency itself is derived rather than written directly on the card.

### Solver

Path: **Analysis → Solver**

Select the backend family, executable, and timeout. `nec2c` is the first fully
supported process adapter. Solver executables are external and are not bundled
with NEC Workbench. Other backend choices remain visible for future adapters.

### Requests

Path: **Analysis → Requests**

Choose the data the solver should produce:

- Structure currents and feed impedance (`XQ`)
- Any number of existing or new far-field radiation requests (`RP`)
- Full-3D, horizontal-cut, vertical-cut, and custom-grid pattern types
- Theta and phi sampling ranges for the selected request
- Which frequencies receive radiation calculations for every RP request

The far-field table lists every RP card from NEC Source. **Add Pattern** and
**Duplicate** create a draft; **Apply Selected Pattern** writes only that row,
and **Delete** removes only the selected RP card. Existing additional RP cards
are never silently overwritten.

Pattern type describes the RP sampling shape, so changing the fixed theta of a
horizontal cut does not make it Custom. **Reset to Preset** restores the standard
angles and steps for the selected Full 3D, Horizontal, or Vertical pattern type.
Sampling arrangements that do not match one of those shapes are labeled Custom.
The global **Pattern Frequencies** selector appears above the request table and
applies to every RP row. Choose the model's complete `FR` sweep, one frequency,
an editable list of separated frequencies, or an independent continuous range.
For selected frequencies, **Amateur Band Centers…** adds one representative
center frequency for each checked band (for example, 20 m adds 14.175 MHz).
It does not add a whole-band sweep; duplicate centers are removed and every
added value remains editable or removable.
Frequency-mode, list, single-frequency, and continuous-range edits highlight
**Apply Pattern Frequencies**. That button commits the global frequency plan
used by every RP request; it does not rewrite any RP card. Leaving Requests with
an unapplied pattern or frequency edit asks whether to return or discard it.
The complete model `FR` sweep still runs for feedpoint impedance and SWR when a
smaller pattern-frequency set is selected. Only the expensive `RP` calculations
are limited. The workload line shows patterns × frequencies and the approximate
angular sample count before running.

Changing a selected pattern type or theta/phi field highlights **Apply Selected
Pattern** because those values belong to that RP card. Applying the row,
selecting another row, or reloading the model clears the pending indicator.
Selected-pattern theta and phi ranges are arranged in
separate side-by-side columns below the table.

The readiness panel explains anything still blocking a run.

Results keep solver RP output blocks separate. Use **Pattern dataset** in the
2D and 3D radiation views to choose the full grid or individual cut. The 3D
view initially prefers the most complete multi-theta, multi-phi dataset.
The 2D viewer automatically selects the compatible orientation for a
horizontal-only or vertical-only dataset and disables orientation switching.
Full grids continue to support both cut orientations.

Radiation summaries identify tied peak directions and report both interpolated
3 dB half-power beamwidth and the width between sampled points. Front-to-back
is shown only when the selected RP data contains the physical direction
opposite the reported peak; otherwise it is marked unavailable.

### Run analysis

Path: **Analysis → Run Analysis**, the main toolbar, or **Run → Run Analysis**

NEC Workbench checks the model again, writes an archived `model.nec`, launches
the selected solver asynchronously, and preserves `model.nec`, `model.out`,
`run.log`, and JSON metadata in a unique run directory. Active runs can be
canceled and are subject to the configured timeout. While a run is active, the
status bar shows an indeterminate activity indicator, the current phase, elapsed
time, growing output-file size, and a Cancel button. The Solver Output dock
continues to show process messages as they become available.
Canceled, timed-out, and failed runs retain their partial artifacts and output
size in Run History, but Workbench does not load or parse the partial `model.out`
on the GUI thread. This keeps stopping a large radiation sweep responsive.

Solver and Requests pages scroll when the window is smaller than their usable
content. Input controls retain their normal text height rather than collapsing;
reduce the content area or use the page scroll bars to reach additional fields.

## Results

### Summary

Results displays the active model's solver output and provides Run History.
Historical runs open in the separate Run Review window. Results and Run History
remain available even when no model is currently open.

Path: **Results** on the top workspace bar

### Detachable Results Window

Use **Detach Results** in the Results banner or **View → Detach Results Window**
to move the complete Results workspace into one reusable top-level window. The
main window returns to the previous modeling workspace, so Model → Geometry or NEC Deck
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
but do not flood the main list. Review the session to restore its candidate table
in Run Review as an archived, read-only session. A segmentation study similarly appears as one **Convergence** row
with hidden child levels. Deleting either session deletes its child runs as a
group.

- Click **Open Run Review** or double-click a run to inspect it in one reusable, separate window without changing the active model.
- Click **Inspect Input Snapshot** to view the run's immutable `model.nec` deck in a read-only window.
- Click **Open Snapshot as New Model** only when you intentionally want the archived deck to replace the editor as an untitled editable copy.
- Click **Open Run Folder** to inspect archived files.
- Click **Delete Run** to permanently remove the selected run directory.
- Use **Cancel Active Run** while a solver process is running.

**Run Review** is separate from the main Results workspace and never contains a
Runs tab, avoiding a circular history-navigation flow. Opening another row updates
the same window rather than creating accumulating windows. Ordinary analyses show
only available Summary, Impedance, Currents, Radiation, and Raw Output pages. AGT,
optimization, and convergence rows instead show their relevant read-only review.

An ordinary run's **Summary** page shows a read-only **Input Snapshot** beside
the result summary. Use its selector to compare the authored `model.source.nec`
with the generated numeric `model.nec` sent to the solver. Older runs show whichever
snapshot is available. The window persistently identifies both the historical run
and the separately active model.

Historical optimization and convergence sessions disable their setup and Run
controls; archived sessions cannot be rerun in place or silently use the currently
edited model. Close their review or use **Return to Current Work** to dismiss the
Run Review window.

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
reactance, impedance magnitude and phase, input power, and SWR referenced to
the model's `Z0`/`ZO` value (50 ohms when omitted). The
**Plots** page shows resistance/reactance and SWR over frequency, with exact
pointer values. Y axes use automatically selected whole-number intervals, and a
separate label gutter keeps the impedance and SWR axis titles clear of tick values.
Each plot has a remembered scale selector. Impedance offers linear and symmetric
logarithmic scales so negative reactance remains visible. SWR offers linear,
logarithmic, and capped 1–3 or 1–5 views; boundary markers identify values clipped
by a cap while hover text continues to show the actual value.

### Currents

Path: **Results → Currents**

Shows per-segment current magnitude and a table of complex current values for
the selected frequency.

### Radiation

Path: **Results → Radiation**

The 2D and 3D pages each show a **Pattern frequency** selector containing only
frequencies for which the solver returned RP data. Each entry also shows the
number of available RP datasets. The two radiation selectors stay synchronized,
but changing them does not move the global Results frequency used by Summary,
Impedance, Currents, and Raw Output. A newly loaded run retains the last viewed
pattern frequency when available; otherwise it selects an available RP frequency.

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

Optimize provides three bounded workflows that share the same `SY` definitions,
frequency plan, reference impedance, weighted objectives, candidate table,
history, and **Apply Best to Model** action:

- **Parameter Sweep** exhaustively evaluates evenly spaced values between the
  bounds using **Sweep points**.
- **Adaptive Optimize** changes every checked parameter together. It begins at
  the center of all ranges plus each parameter's minimum and maximum boundary,
  then performs progressively smaller coordinate trials around the best candidate.
  It stops at the evaluation budget, per-parameter tolerance, or after repeated
  rounds remain within the score-improvement tolerance.
- **Nelder–Mead** changes every checked parameter using a derivative-free simplex.
  It reflects the least useful point, expands promising moves, contracts weak moves,
  and shrinks the simplex when necessary. It is usually the stronger general-purpose
  choice for interacting continuous parameters.

An adaptive refinement round can evaluate a lower and upper coordinate trial for
each selected parameter while holding the other parameters at the current best
values. The score tolerance compares the best result after the complete round,
not each candidate separately.
The search stops after two consecutive completed rounds fail to improve the best
score by more than the configured tolerance. Candidate status labels identify the
initial samples and each round's left and right trials.

Adaptive Optimize is a transparent derivative-free coarse-to-fine search. It is
more efficient than a dense sweep when the useful region is localized, but a
parameter sweep remains valuable for inspecting the full objective landscape.
Nelder–Mead is also derivative-free, but its simplex can move several parameters
together instead of considering only one coordinate direction at a time. Bounds
are enforced on every proposed candidate. It stops at the evaluation budget, when
the simplex fits within every parameter tolerance, or when a contracted simplex's
scores remain within the score tolerance.
The latest candidate table remains visible when moving to another workspace and
returning to Optimize. It resets only after the active model source changes or a
new sweep begins.

The Optimize workspace keeps one compact setup pane on the left and gives the
expanding right side to results. Choose **Parameter Sweep** or **Adaptive
Optimize**, or **Nelder-Mead** from the mode selector above the workspace. The same row shows the
active variable, range, frequency count, objective, and Run/Stop controls.

The left pane keeps **Variable**, **Frequencies**, and **Objective** visible as
sections of one study rather than separate setup pages. Parameter Sweep shows a
single variable and range. Adaptive Optimize and Nelder–Mead show a compact table where each
checked parameter has its own Minimum, Maximum, and Tolerance; double-click those
cells to edit them. Frequencies and Objective show the
active source, range, criteria, targets, and reference impedance as compact
summaries; use their **Edit…** buttons for the full controls. Accepting an editor
keeps the changes, while Cancel restores the complete prior setup. Only
method-specific controls change when the search mode changes. The pane scrolls
independently on smaller displays and can be resized with the horizontal divider
without forcing the results area into a second page.

The right pane shows the candidate table and objective plot together, separated
by an adjustable vertical divider. The plot shows total objective score against
the swept parameter for a single-variable study, or evaluation number for a
multivariable study, together with the enabled weighted SWR, resistance, and
reactance contributions. Lower values are better, and the best candidate is
marked. Hover for exact values; double-click a marker to open the same
candidate-detail window used by the candidate table. That window offers both a
frequency table and SWR/impedance plots generated from the candidate's retained
feedpoint results. Inspecting these plots does not create a normal analysis run
or replace the active model. Decimal values are displayed and entered to three
places throughout the optimizer; archived raw solver output remains unchanged.

**Reset Search Defaults** restores Candidate count to 7 for Parameter Sweep, or
Maximum evaluations to 21 and Score tolerance to 0.001 for Adaptive Optimize.
It does not change selected parameters, their ranges and tolerances, the frequency
plan, or objective. Optimizer numeric fields respond to the mouse wheel only
while focused, preventing accidental changes while scrolling the setup area.

For **Minimax**, **Frequency Source** controls the
frequencies calculated for every candidate:

- **Use Model FR Sweep** keeps the model's existing `FR` definition.
- **Use Selected Frequencies** evaluates only the explicit MHz values in the
  responsive wrapped grid. Add values individually or paste a list separated by spaces,
  commas, semicolons, or new lines. This supports separated bands without
  calculating every frequency between them. Select rows and press **Delete** or
  **Backspace** to remove them. **Clear All** empties only this editable list
  after confirmation; it does not change the model's `FR` card.
- **Add Amateur Bands…** opens a checkbox dialog for adding several common band
  ranges at once. The dialog shows the resulting point count; added frequencies
  remain editable and duplicate points are removed automatically. Presets are
  modeling conveniences, not statements of local operating privileges.
- **Custom Continuous Sweep** evaluates an independent linear range using the
  entered start, stop, and step frequencies. These points apply only to the
  parameter sweep and do not rewrite the active model's `FR` card.

The selected frequency source, editable frequency list, and custom continuous
sweep values remain available while moving between workspaces, refreshing the
same model, or temporarily inspecting a historical optimization session. They
reset when a different model is opened and are not retained after Workbench exits.

The workload summary shows candidate count × frequency count before the sweep.

The symbol table keeps authored and evaluated values separate:

- **Expression** is the exact text authored after `=` on the `SY` card.
- **Resolved Value** is the unit-neutral numeric result of evaluating that
  expression.

Workbench does not infer a physical unit merely because a symbol appears in a
`GW` field. For example, `SY LONG_FT=95` remains the raw value `95.000`; its
meaning as feet comes from the author's later expression such as `LONG_FT*FT`.
Likewise, `GS` scales geometry at the NEC-card usage site and does not change the
raw `SY` value. The parameter sweep offers both direct numeric assignments and
calculated expressions in its variable selector. Selecting an expression-based
symbol overrides its resolved value for that candidate before later symbol
expressions and NEC cards are evaluated. Workbench does not try to determine
whether the selected symbol materially affects the final model.

Fixed continuous numeric fields can be promoted without manually editing raw source.
Right-click a wire coordinate or radius in **NEC Source → Structured Cards → Wires**, or
a continuous numeric field in **Other Supported Cards**, then choose **Parameterize Field…**.
The dialog can create an `SY` definition initialized to the existing value or link the field
to an existing parameter. Parameter-controlled fields offer **Change Parameter Link…** and
**Replace With Current Numeric Value** on their context menu. Set new parameter bounds in
Optimize. Integer and categorical fields remain excluded until discrete optimization is supported.
Parameter-controlled cells use a subtle accent, italic text, and an `ƒx` icon; hover over
one to see its source expression. Other arithmetic expressions remain read-only without
the parameter accent.

For Parameter Sweep, “Complete” means every requested candidate was attempted;
it is not convergence. Adaptive Optimize and Nelder–Mead report their stopping
reasons separately.
Future tolerance analysis will perturb a finalist to measure construction and
component sensitivity.

**Minimax** minimizes each candidate's worst-performing weighted objective point
across the selected frequencies. **Selected Frequency** calculates and
scores only the requested frequency, regardless of the model's authored `FR`
sweep.
Hover over either evaluation choice for a concise description of its frequency
and scoring behavior.

The **Weighted Objectives** grid combines three impedance criteria. A weight of
zero disables that criterion. SWR is minimized directly; resistance and
reactance minimize their distance from the entered targets. Resistance and
reactance errors are divided by the reference impedance before weighting, then
the combined score is divided by total weight. This keeps values in ohms from
dominating SWR merely because their raw numbers are larger. The default weights
preserve the original SWR-only behavior. The results table shows the normalized
objective score separately from actual SWR, resistance, and reactance. The
candidate plot uses these exact evaluator contributions rather than recalculating
or approximating the score in the GUI.

Radiation requests are omitted from these initial impedance-only candidates to
keep the sweep fast. Each candidate remains available in Results → Run History
and contains `model.source.nec`, the generated numeric `model.nec`, solver output,
and `optimization.json` metadata. The results table identifies the frequency and
feedpoint impedance that produced each score. Hover over a candidate for guidance,
then double-click it to open one reusable, non-modal frequency-results window.
That window shows SWR, resistance, and reactance at every calculated frequency;
the frequency that determines the objective is bold. Double-clicking another row
updates the same window instead of opening another copy. The window is explicitly
labeled as optimization-candidate data rather than official active-model results.
After a successful study, **Apply Best to Model** replaces every optimized `SY`
expression with the winning candidate's numeric values in one explicit, undoable
source edit. The candidate window also provides **Apply This Candidate to Model**
for a manually selected row and **Apply This Candidate and Run**. The latter applies
that candidate and starts a normal analysis with the active model's current Analysis
requests, creating an ordinary run and complete Results entry. Applying alone does
not run the solver. The completed study, candidate frequency table, and SWR/R/X
plots remain visible after either apply action so the applied candidate can still
be reviewed. A later unrelated model edit clears the now-stale optimization study.
Archived candidate runs remain unchanged.

Parameter Sweep submits each value to the shared Candidate Evaluator used as the
foundation for future optimizer algorithms. That service resolves symbols,
generates and validates the numeric deck, runs the configured solver, parses its
output, and calculates the selected objective. A shared Frequency Plan supplies
model sweeps, explicit points, and preset ranges to the evaluator without
changing the visible sweep workflow.

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

Use **Model → Parameters** for normal `SY` definition editing. Raw Source remains
available for expert deck editing. Geometry edits continue to protect symbolic
`GW` fields rather than replacing authored expressions with resolved numbers.

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

Shows the active model and categorized NEC cards. Single-click synchronizes
supported model-object selections; double-click navigates to the most appropriate
editor. Wire radius and AWG remain available through the wire Properties dialog.

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
2. Build or inspect wires in Model → Geometry or Model → NEC Deck.
3. Configure sources and ground under Model.
4. Add loads or transmission lines under Model → Loads & Networks if needed.
5. Configure frequency and the solver under Analysis.
6. Choose current/impedance and radiation requests.
7. Run Check Model and resolve blocking diagnostics.
8. Run Analysis.
9. Inspect numerical, sweep, current, and radiation results.
10. Reopen the preserved run later from Results → Run History.
