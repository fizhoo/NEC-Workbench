# NEC Workbench User Guide

NEC Workbench organizes antenna modeling into five workspaces across the top of
the main window: **Home**, **Model**, **Analysis**, **Results**, and **Optimize**.
The Model workspace's compact left navigator contains **Geometry**, **Parameters**,
**Sources**, **Loads & Transmission Lines**, **Environment**, and **NEC Deck**.
Analysis and Results use the same navigation pattern. These are work areas rather
than a required step-by-step wizard. Use the arrow above a workspace navigator to
collapse it to icons when screen space is limited; the choice is remembered.

The desktop menus provide the complete command inventory:

- **File** contains New, Open, Save, Save As, and Exit.
- **Edit** contains Undo, Redo, and focus-aware Cut, Copy, and Paste commands.
- **View** controls the Project dock, Model Check Report, Run Monitor, detached
  Results, and **Reset Layout**. Reset Layout reattaches Results and restores the
  default dock arrangement without changing the model. Project is automatically
  hidden in the space-intensive Results and Optimize workspaces unless it is floating;
  its prior visibility returns when you leave those workspaces.
- **Model** contains Check Model, Automatic Segmentation, Average Gain Test,
  Segmentation Convergence, and Geometry Settings.
- **Run** contains Run Analysis, Quick Frequency Sweep, and a global Stop command
  for the active analysis, validation study, or parameter sweep.
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
structured view, and reports errors and warnings in the reusable Model Check Report.
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

The top workspace bar and each workspace's compact left navigator provide the
primary navigation. Home therefore avoids duplicate Model, Analysis, and Results
destination buttons. Its heading
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
When a backend such as OpenNEC returns the requested dense radiation grid but
does not print an `AVERAGE POWER GAIN` summary, Workbench integrates the grid
over its full sphere or hemisphere and records that derived AGT value.

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

Workbench also displays wire arcs (`GA`), helical `GH` geometry, tapered `GW`/`GC`
wires, and wire structures transformed by `GM`, `GX`, or `GR`. These generated
paths retain their authored NEC cards and are read-only in the graphical editor;
selection and segment-based source, load, and transmission-line placement still
work. Flat-spiral `GH` geometry is not yet expanded.

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

Generated `GA`, `GH`, `GC`, `GM`, `GX`, and `GR` paths can be inspected and selected
in 3D, but their shape or transformation must be edited in Raw Source or Other
NEC-2 Geometry Cards.

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
- **GA Wire Arcs** — tag, segments, arc radius, start/end angles, and wire radius
- **GH Helices and Spirals** — tag, segments, turn spacing, axial length,
  start/end elliptical radii, and wire radius
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

GA and GH dimensional columns use the authored NEC deck length unit. Their field
tooltips describe the NEC meaning, and adding either card places it before `GE`.
Helical GH cards update the graphical model; valid flat-spiral GH cards remain
source-editable but are not yet expanded graphically.

Workbench recognizes the complete standard NEC-2 card vocabulary. `GM`, `GX`, and
`GR` remain in the generic geometry-card table while their effects are applied to
the graphical wire model in authored order. Remaining generators and additional
control cards appear under **Other NEC-2 Geometry Cards** and **Other NEC-2 Control
Cards**. These tables safely edit numeric fields without claiming dedicated editors.
Unknown extensions remain preserved in Raw Source. See
[NEC Card Support](nec-card-support.md) for the support level of each mnemonic.

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
supported reference adapter. **OpenNEC** invokes `onec` with positional input,
requests original NEC-2 report output, and writes the named output file.
**4nec2 NEC2dXS** starts the selected executable without arguments and supplies
the input and output filenames through standard input. The executable browser can
select any NEC2dXS capacity build; selecting a larger build does not alter model
segmentation, although it may use more memory. When a standard NEC2dXS filename is
recognized, Workbench reports its compiled segment limit and blocks a clearly
oversized active model. Solver executables are external and are not bundled with
NEC Workbench.

Workbench remembers a separate executable path for each backend. Switching from
nec2c to OpenNEC or NEC2dXS restores the last path selected for that engine; use
the existing **Backend** list and **Browse…** button to change it. These are user
preferences, not NEC-model data. Qt stores them in the current user's native
settings location: the user configuration area on Linux and the current-user
application settings in the Windows registry.

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
time, growing output-file size, and a Cancel button. The non-modal Run Monitor
opens automatically and shows the exact command, run folder, live process messages,
and Stop, Copy Command, Open Run Folder, and Hide controls. Hiding it does not stop
the solver; **View → Run Monitor** reopens and pins the same window. An automatically
opened monitor hides shortly after a successful run, while failed, timed-out, or
canceled runs remain visible for diagnosis. A monitor opened manually stays open.
Canceled, timed-out, and failed runs retain their partial artifacts and output
size in Run History, but Workbench does not load or parse the partial `model.out`
on the GUI thread. This keeps stopping a large radiation sweep responsive.

### Quick frequency sweep

Path: the main toolbar or **Run → Quick Frequency Sweep** (`Ctrl+F6`)

Quick Frequency Sweep evaluates the checked model over a temporary linear or
logarithmic frequency range without changing its authored `FR` card. Linear mode
uses a fixed MHz step; logarithmic mode uses a selected point count. The dialog
shows the actual endpoint and number of generated frequencies before running.

The default fast mode removes `RP` requests from the generated deck and calculates
impedance, SWR, and currents with an inserted `XQ`. Enable **Include existing RP
radiation requests** to retain the model's patterns at every sweep frequency; this
can substantially increase solver time and output size.

Run History identifies these records as **Quick Sweep**. `model.source.nec` retains
the unchanged authored model while `model.nec` contains the temporary `FR` override,
providing reproducible results without modifying or dirtying the editor.

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
that window instead of replacing the main workspace. If it was hidden or minimized,
the same window is restored to the foreground without reattaching it.

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
and cut angle controls. Ring and angle labels use the active Qt theme's primary
text color while the grid remains subdued, preserving contrast across Windows
and Linux light and dark palettes. Move the pointer over the plot to highlight
the nearest actual NEC sample and display its angle, absolute dBi, and relative
dB in the dedicated readout directly above the graph. This readout does not
interpolate between solver samples.

- Left/Right changes the available cut angle.
- Space switches horizontal and vertical cut orientation.
- **Show Peak Cut** moves to the sampled cut containing maximum gain.

The **3D Pattern** page can layer the archived antenna, current distribution,
and radiation surface. Orbit, pan, and zoom use the same controls as 3D Geometry.
Layers can be hidden independently. The radiation mesh is colored from the
selected dynamic-range floor through the displayed peak, with a labeled scale
that distinguishes relative dB from absolute dBi. Move the pointer over the
surface to highlight the nearest calculated NEC sample and display theta, phi,
absolute gain, and gain relative to the pattern peak. The probe reports solver
samples rather than interpolated surface values.

The compact **Performance vs Frequency** page plots forward gain, F/B, and F/R
together for a configured physical theta/phi direction. Its summary reports the
minimum, average, and maximum of every available trace and identifies the
frequencies producing each extreme. Hover the plot for exact points. The current
Results frequency is marked without filtering the traces. Component selection is
synchronized with the 2D and 3D pages.

Performance calculations use all available RP samples in the run. Gain requires
an exact sample at the configured forward direction; F/B additionally requires
the physical antipode; F/R requires rear-half samples at the configured theta.
Missing directions remain unavailable rather than being interpolated or replaced
with a pattern minimum. This means older runs may show only the traces supported
by their original RP requests.

### Raw Output

Path: **Results → Raw Output**

Displays the complete, unchanged solver `model.out` for auditing or diagnosing
output that is not yet parsed into a structured result view. Changing Result
Frequency does not rewrite this authoritative artifact. Use **Jump to Selected
Frequency** to position the editor at the matching solver section, or **Find
Next** for free-text search. Process messages remain in the Run Monitor.

## Optimize

### Summary

Path: **Optimize** on the top workspace bar

Optimize provides four bounded workflows that share the same `SY` definitions,
frequency plan, reference impedance, weighted objectives, candidate table,
history, and **Apply Best to Model** action:

See [Optimization Mathematics](optimization-math.md) for the implemented score
equations, candidate-generation formulas, constants, and stopping rules.

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
- **Differential Evolution** changes every checked parameter using a seeded
  population. Mutation and crossover explore separated regions of the bounded
  search space, making it useful when several local minima may exist.

An adaptive refinement round can evaluate a lower and upper coordinate trial for
each selected parameter while holding the other parameters at the current best
values. The score tolerance compares the best result after the complete round,
not each candidate separately.
The search stops after two consecutive completed rounds fail to improve the best
score by more than the configured tolerance. Candidate status labels identify the
initial samples and each round's left and right trials. At completion, Workbench
reports the final two best-score improvements, selects and centers the winning
candidate, and warns when a winning value reaches a search bound because a wider
range may contain a better solution. Parameter tolerance controls candidate spacing;
score tolerance controls objective convergence, so the two values are not comparable.

Adaptive Optimize is a transparent derivative-free coarse-to-fine search. It is
more efficient than a dense sweep when the useful region is localized, but a
parameter sweep remains valuable for inspecting the full objective landscape.
Nelder–Mead is also derivative-free, but its simplex can move several parameters
together instead of considering only one coordinate direction at a time. Bounds
are enforced on every proposed candidate. It stops at the evaluation budget, when
the simplex fits within every parameter tolerance, or when a contracted simplex's
scores remain within the score tolerance.
Differential Evolution first evaluates the current model and a random bounded
population, then evolves one trial for each population member per generation.
Population controls search breadth, Generations controls the evaluation budget,
Mutation factor controls exploratory step size, and Crossover rate controls how
many parameter values enter each trial. Reusing the same random seed and settings
reproduces the same proposed candidates. It stops at the generation limit, when
the population fits within every parameter tolerance, or after three generations
whose best-score improvement is no greater than the score tolerance.
The latest candidate table remains visible when moving to another workspace and
returning to Optimize. It resets only after the active model source changes or a
new sweep begins.

The Optimize workspace keeps one compact setup pane on the left and gives the
expanding right side to results. Choose **Parameter Sweep**, **Adaptive
Optimize**, **Nelder-Mead**, or **Differential Evolution** from the mode selector
above the workspace. The same row shows the
active variable, range, frequency count, objective, and Run/Stop controls.

The left pane keeps **Variable**, **Frequencies**, and **Objective** visible as
sections of one study rather than separate setup pages. Parameter Sweep shows a
single variable and range. Adaptive Optimize, Nelder–Mead, and Differential
Evolution show a compact table where each
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
reactance, forward-gain, front-to-back, and front-to-rear contributions. Lower values are better, and the best candidate is
marked. Hover for exact values; double-click a marker to open the same
candidate-detail window used by the candidate table. That window offers both a
frequency table and SWR/impedance plots generated from the candidate's retained
feedpoint results. Inspecting these plots does not create a normal analysis run
or replace the active model. Decimal values are displayed and entered to three
places throughout the optimizer; archived raw solver output remains unchanged.

**Reset Search Defaults** restores Candidate count to 7 for Parameter Sweep,
Maximum evaluations to 21 and Score tolerance to 0.001 for Adaptive Optimize and
Nelder–Mead, or the default population, generations, mutation, crossover,
score-tolerance, and seed values for Differential Evolution.
It does not change selected parameters, their ranges and tolerances, the frequency
plan, or objective. Optimizer numeric fields respond to the mouse wheel only
while focused, preventing accidental changes while scrolling the setup area.

**Frequency Source** controls the frequencies calculated for every candidate:

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

Structured Cards displays the canonical workflow directly above its card tabs:
double-click to edit, or right-click a numeric cell to parameterize it or link an
existing `SY`. **Model → Parameters** manages symbol names and expressions and
points back to this same field-linking workflow. There is only one underlying
parameterization system; these are contextual entry points into the authoritative
NEC source, not separate parameter stores.

For Parameter Sweep, “Complete” means every requested candidate was attempted;
it is not convergence. Adaptive Optimize, Nelder–Mead, and Differential Evolution report their stopping
reasons separately.
Future tolerance analysis will perturb a finalist to measure construction and
component sensitivity.

The **Objective** editor uses one row for each criterion: SWR, resistance,
reactance, forward gain, F/B, and F/R. A weight of zero disables the row. Every
enabled row independently defines:

- **Goal** — Minimize, Maximize, Target, Good Enough ≤, or Good Enough ≥.
- **Value** — the target or acceptable threshold; it is disabled when the selected
  goal does not need one.
- **Band Evaluation** — Worst Point, Average, or Best Point over the Frequency
  Source plan. Target and Good Enough goals use the clearer Worst/Average/Best
  Error or Violation wording.

This replaces the old global Minimax/Average/Selected Frequency switch. To evaluate
one frequency, choose a one-point frequency plan. To protect the weakest point over
a band, use **Maximum** for a minimized error such as SWR or **Minimum** for a
maximized measurement such as gain. **Target** scores absolute error from the entered
value. **Good Enough** scores only the amount by which the selected threshold is
violated, so a satisfied criterion contributes zero.

**Restore Objective Defaults** returns to the practical starting configuration:
SWR minimizes its maximum, resistance targets 50 Ω using maximum error, reactance
targets 0 Ω using maximum error, and gain/F/B/F/R maximize their minimum. Only SWR
has a nonzero default weight.

An **Objective Summary** below the grid translates every enabled row into a bullet,
such as “Minimize the highest SWR across the selected frequencies” or “Maximize
the lowest forward gain.” It also shows the relative weight, so the complete study
can be reviewed without mentally decoding the table.

Resistance and reactance errors are divided by the reference impedance before
weighting. Gain, F/B, and F/R are scaled in 10 dB units. The combined score is
divided by total enabled weight, keeping one unit from dominating merely because
its raw values are larger. Lower total scores are always better. The results table
shows that normalized score separately from measured values, and the candidate plot
uses the evaluator's exact contributions rather than recalculating them in the GUI.

**Forward gain**, **Front-to-back**, and **Front-to-rear** are directional criteria. Enter the physical
forward `theta` and `phi` direction and choose Total, Vertical, Horizontal, RHCP,
or LHCP. They use the same frequency plan and per-row Goal and Band Evaluation
settings as impedance criteria. Different criteria may therefore be reduced from
different frequencies in the same candidate.
Forward gain uses the gain at that exact spherical direction. Front-to-back uses
that gain minus the gain exactly 180° opposite; it never substitutes the pattern
minimum, an endpoint, or the nearest unrelated angle. If the required directional
sample is absent from solver output, that candidate is unavailable rather than
receiving a misleading score.

Front-to-rear uses the forward gain minus the strongest gain in the rear 180°
half of the azimuth cut at the configured theta. Workbench samples that rear
region every 5°, including its ±90° boundaries. This phase-one definition is a
documented 2D azimuth-cut measurement, not a search over the full rear hemisphere.

Directional criteria default to Maximize, but may also be minimized, targeted, or
given a Good Enough threshold. A maximize contribution is `-measured / 10` before
weight normalization, so a higher value improves the lower-is-better total score.
Workbench adds one exact one-point `RP` request at each study frequency for forward
gain and a second request at each frequency for the physical opposite direction when
F/B is enabled. F/R adds 37 rear-region samples per frequency, so the workload
summary makes its greater cost visible before the study begins. The candidate table
shows measured **Gain (dBi)**, **F/B (dB)**, and **F/R (dB)** columns, using an em dash when that
directional result was not requested. Historical optimization sessions reconstruct
the same values and limiting frequency from their archived solver output. The Objective editor
states this cost before the study starts.

Candidate details report each enabled criterion's Goal, frequency reduction,
reduced value, weighted contribution, and available minimum/average/maximum
statistics. Minimum and maximum summaries identify the frequency that produced
each extreme; an average is explicitly band-wide and has no single associated
frequency. The frequency table includes SWR, R, X, forward gain, F/B, and F/R,
with an em dash where a metric was not requested. A dedicated Directional Plots
view graphs gain, F/B, and F/R against frequency and repeats their extrema above
the plot. A multi-frequency candidate leaves the main Frequency column blank
because no single frequency necessarily determines its score. A one-frequency
study displays and bolds that evaluated frequency.

Hover **Goal** and **Band Evaluation** choices for exact scoring semantics. The
tooltips distinguish measured values, target error, and threshold violation, and
explain which frequency is treated as the worst or best for the selected goal.

Broad model radiation requests are omitted from optimization candidates to keep
the sweep fast. Impedance-only objectives emit no `RP`; directional objectives
emit only the directional samples described above. Each candidate remains available in Results → Run History
and contains `model.source.nec`, the generated numeric `model.nec`, solver output,
and `optimization.json` metadata. Hover over a candidate for guidance,
then double-click it to open one reusable, non-modal frequency-results window.
That window shows SWR, resistance, and reactance at every calculated frequency;
the row is bold only when the study contains one frequency. Double-clicking another row
updates the same window instead of opening another copy. The window is explicitly
labeled as optimization-candidate data rather than official active-model results.
Returning to Optimize restores an already-open candidate window if it was hidden
or minimized; it is never duplicated or reattached automatically.
After a successful study, **Apply Best to Model** replaces every optimized `SY`
expression with the winning candidate's numeric values in one explicit, undoable
source edit. The candidate window also provides **Apply This Candidate to Model**
for a manually selected row and **Apply This Candidate and Run**. The latter applies
that candidate and starts a normal analysis with the active model's current Analysis
requests, creating an ordinary run and complete Results entry. Applying alone does
not run the solver. The completed study, candidate frequency table, SWR/R/X plots,
and directional plots remain visible after either apply action so the applied candidate can still
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

### Model Check Report

Shows source/card errors, readiness warnings, compatibility notices, and static
model-adequacy findings in separate categories. Click a finding to jump to its
source line. A clean static report does not replace **Results → Validation →
Average Gain Test** or segmentation convergence testing. A clean check uses the
status bar only; the report opens automatically when findings require attention.
The same non-modal window is reused and remains available from **View**.

### Run Monitor

Shows the command, process messages, standard output, and standard error from
the active run. It opens automatically when a normal analysis, Quick Sweep, or
Average Gain Test starts. Normal run progress also remains visible in the status
bar. It hides shortly after success unless opened manually, and remains visible
when a run needs attention. Optimization candidates continue to use the Optimize
workspace progress UI.

### Undo and Redo

Path: **Edit → Undo/Redo** or the platform's standard Undo/Redo shortcuts

All committed model changes share one source-backed Undo/Redo history. This
includes direct Raw Source typing, Structured Cards, geometry edits, parameters,
sources, loads, networks, environment, and analysis-request changes. Undo and
Redo work from any workspace and refresh the parsed model and synchronized views.
The menu and toolbar identify the next operation, such as **Undo Change frequency
to 14.200 MHz**. Afterward, the status bar reports what was undone or redone and
Workbench returns to the affected editor when practical. Direct typing is labeled
**Raw Source Edit**.
Pending form edits are not added to history until **Apply** is selected; use the
form's Revert or Cancel action to discard unapplied values.

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
