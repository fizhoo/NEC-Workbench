# NEC Workbench User Guide

NEC Workbench uses **Home**, a compact control panel, as its operating base. The
Geometry Editor, NEC Deck, Setup, and Optimize open as reusable work windows, and the
result launchers open focused live-result windows and Run History. Controls provide tooltips; this guide focuses on workflow and
behavior that is not obvious from an individual control.

## Getting Started

1. Create or open a `.nec` model from Home or the File menu.
2. Edit wires in the **Geometry Editor** and cards in the **NEC Deck** window.
3. Configure frequency, sources, loads, environment, requests, and solver under Setup.
4. Run **Check Model** (`F7`) and resolve blocking errors.
5. Select **Run Analysis** (`F5`).
6. Open the Impedance / SWR, Currents, or Radiation windows from Home's RESULTS
   launchers or **View → Result Windows**. Use Run History and Run Review for archived
   input and historical raw output.

NEC Workbench does not include an NEC solver. Configure a separately installed
`nec2c`, OpenNEC, or 4nec2 NEC2dXS executable under **Setup → Solver**.

The selected backend also controls compatibility validation. **NEC-2 / nec2c**
uses strict original NEC-2 card rules, while **OpenNEC** accepts the verified
4nec2 extensions that it implements. Workbench always resolves `SY` symbols and
formulas into a numeric generated solver deck while preserving the authored source.
The Model Check report identifies backend-specific incompatibilities and links back
to Solver settings; **View Generated Solver Deck** shows the numeric base deck used
for execution.

The **4nec2 NEC2dXS** adapter launches the calculator executable directly. Features
normally preprocessed by the full 4nec2 application, beginning with `LD 6` LC traps,
remain blocked until equivalent Workbench conversion is available. Workbench writes
the numeric NEC2dXS solver copy with canonical space-delimited fields because the
calculator's NEC-2 reader does not recognize tab characters as field separators.
Authored tabs, commas, spacing, and comments remain unchanged in `model.source.nec`;
only the archived `model.nec` sent directly to NEC2dXS is normalized.

## Application Layout

The main window is Home, which is fixed to the size of its content. Everything else
opens in its own window: the Geometry Editor, NEC Deck, Setup, and Optimize work
windows, plus the Impedance / SWR, Currents, Radiation, and Run History windows that
can stay visible during edits and reruns. Each window remembers its own size and
position, and opening one again raises the existing window rather than creating a
duplicate. Historical Run Review remains a separate read-only window. Inside Setup and
the NEC Deck window, a compact navigator selects pages; collapse it to icons when
screen space is limited.

The menu bar holds the complete command set. Important actions include:

- **File → Open Recent**
- **View → Result Windows** — Impedance & SWR, Currents, Radiation, Raw Solver
  Output, and Run History
- **View → Workspace Density**, **Project Tree**, and **Reset Layout**
- **Model → Geometry Editor**, **NEC Deck**, and **Automatic Segmentation**
- **Validate → Check Model**, **Average Gain Test**, **Segmentation Convergence**,
  and **Open Validation Results**
- **Run → Run Analysis**, **Quick Frequency Sweep**, and **Stop**
- **Run → Review Run History** and **Open Run Folder**

Workspace Density offers Compact, Standard, and Spacious spacing without changing
the application font size. Use Compact on smaller displays.

The Project Tree is hidden by default; show it from the View menu when you want an
outline of the model.

## Home

With no model open, Home provides New, Open, recent files, and documentation.
With a model open, it becomes a compact control panel:

- **Commands** — Open (its arrow lists recent models), Save, Run (`F5`), Frequency
  Sweep, Stop, and Check Model. The header shows the model, its check status, and the
  solver engine; click the engine to configure it.
- **Geometry Editor** and the **RESULTS** launchers for Impedance / SWR, Currents,
  and Radiation open or raise those windows; **Runs…** and **Optimize…** sit below.
- **MODEL** lists frequency, wires, sources, loads, transmission lines, ground,
  variables, requests, and deck size. Click a row to open its editor: Wires opens the
  Geometry Editor, Deck and Variables open the NEC Deck window, and the setup rows
  open Setup.
- **QUICK RESULTS** shows Rin, Xin, SWR, gain, and F/B at the frequency chosen in
  **At:**, plus the sweep's minimum SWR and peak gain. Click a value to open its result
  window. **STALE** appears when the model has changed since the run.
- **MODEL QUALITY** shows the Average Gain Test and segmentation convergence status,
  each with its own Run button.

The footer shows the last run and whether the displayed results are current or stale.
Run progress appears in the Run Monitor rather than on Home.

## Model

The model is edited in two windows, each of which remembers its own size and
position: the **Geometry Editor** (2D and 3D geometry) and the **NEC Deck** window
(Raw Source, Structured Cards, and Parameters). Keep the deck window small beside your
runs and the Geometry Editor large for drawing; switching between them does not
resize either one.

### Geometry

The XY, XZ, YZ, and 3D views share selection with Structured Cards and Project.
Ordinary `GW` wires can be selected and edited. The 2D views support endpoint and
whole-wire dragging, snapping, fitting, panning, and zooming. Right-click a wire
for properties, splitting, deletion, source creation, or AWG sizing.

Generated geometry—such as arcs, helices, transformations, and surface patches—is
displayed when supported but remains graphically read-only. Edit its originating
card in Structured Cards or Raw Source.

Display units do not change model dimensions. Deck units and ordered `GS` scaling
remain separate from display and snap settings.

### Parameters

Model Parameters manages existing `SY` names and expressions. Resolved Value is
the evaluated number, not an inferred physical unit.

To parameterize a field:

1. Open **Model → NEC Deck** and choose the **Structured Cards** tab.
2. Right-click a continuous numeric cell.
3. Create a new parameter or link an existing `SY` definition.

Parameter-controlled cells are marked and retain their source expressions. Integer
and categorical fields are not currently optimization variables. Parameterizing a
negative numeric field creates a positive parameter magnitude and preserves the field
sign in the reference. Linking an existing parameter similarly preserves an equal or
opposite current value, which supports symmetric geometry such as `-half_length` and
`half_length` without changing the model.

### NEC Deck

Raw Source and Structured Cards operate on the same document.

- Use Raw Source for unrestricted deck editing and unsupported extensions.
- Use Structured Cards for labels, choices, validation, and source mapping.
- Double-click an editable cell to change it.
- Long numbers, such as those written by geometry drags or the optimizer, are shown
  at 6 significant figures to keep columns narrow. The deck keeps the exact value:
  hover a cell to see it, and editing starts from it.
- Right-click a numeric cell to parameterize it.
- Selecting a structured row moves the raw-source cursor to the same card.

Unknown cards and formatting are preserved. Recognition does not imply that every
card has a dedicated editor or graphical representation; see
[NEC Card Support](nec-card-support.md).

For the exact backend profiles, card checks, severity rules, and current audit
boundaries, see the [NEC Validation Reference](nec-validation-reference.md).

### Undo and Redo

Source-backed model changes share one history. Undo may reverse a change made in
another workspace and may navigate to that context. Solver runs, result selection,
window layout, and historical review are not model edits.

## Model Checking and Adequacy

**Check Model** parses source, resolves symbols, rebuilds semantic views, and
reports errors and warnings. Errors block analysis and optimization. Warnings
identify conditions requiring engineering judgment.

The **Validate** menu groups the static Model Check, Average Gain Test, and
segmentation convergence commands. **Validate → Open Validation Results** opens a
reusable focused window with the compact status overview and both solver-backed
studies. The full static report remains a separate non-modal window so findings can
stay visible while the model is edited.

Static checks include card ordering, geometry completeness, field types, references,
segment length, thin-wire ratios, source placement, adjoining segment consistency,
and large junctions. Static checks do not prove model accuracy.

Control-sequence checks follow the NEC-2 execution model rather than imposing one
fixed control-card order. Parameter cards must precede the execution request they
are intended to affect. After a multi-frequency request executes, repeat `FR`
before another execution group if that later request must traverse the full sweep.
During automatic frequency stepping, `NE` and `NH` require a following `XQ` or
`RP`. Workbench reports unused or ambiguous sequences but never silently reorders
the authored deck. See the [NEC-2 control-card documentation](https://www.nec2.org/part_3/control.html).

Leading `CM` comment cards must be terminated by `CE` before geometry begins. Every
complete deck must contain one final `EN` card; blank lines may follow it, but no
additional NEC cards may appear afterward.

### Automatic Segmentation

Automatic Segmentation previews proposed segment counts using the highest modeled
frequency and selected segments per wavelength. Applying the preview is one
undoable model edit. Supported source, load, and transmission-line references are
remapped by relative position; unsupported references block the operation. Decks
that use one tag number on more than one wire, or that refer to segments by absolute
number (tag 0), are also blocked for now, as is segmentation convergence testing.

### Average Gain Test

Average Gain Test creates a temporary one-frequency, lossless deck in free space
or over perfect ground. It does not modify the authored model. Current AGT and
convergence results share the reusable **Validation** window; completed studies are
also archived in Run History.

### Segmentation Convergence

Convergence runs progressively refined versions of the same model and compares
successive impedance and available gain results. Tolerances describe numerical
stability between levels, not universal model correctness.

## Setup

### Sources, Loads, Networks, and Environment

These pages provide structured editing for supported `EX`, `LD`, `TL`, `GN`, and
related model setup. Draft rows do not change the source until Apply is selected.
An emphasized Apply button indicates pending changes.

References are validated against available wire tags and segments. Units shown in
the interface are converted to the NEC values stored in source.

### Frequency

Edit the model's supported `FR` definition here. A sweep is represented by start,
spacing, and point count; the displayed final frequency is derived from those values.

**Quick Frequency Sweep** runs a temporary linear or logarithmic sweep without
rewriting the authored `FR` card.

### Solver

Select the backend protocol and executable. Each backend remembers its own path.
The backend selection controls command-line or standard-input behavior; Workbench
does not infer protocol from the executable filename.

The **Run** menu provides direct access to the reusable Run Monitor and archived
Run History. **Run → Open Run Folder** opens the folder of the run started in this
session, or the newest stored run, in the system file browser. The monitor is transient execution feedback; durable inputs, complete
raw output, notes, and read-only review remain in Run Review.

### Model Check

Choosing **Check Model** always opens the complete report, including successful
automatic conversions such as `SY` expansion. Automatic checks after edits update
the report silently when there are only informational findings; warnings and errors
still open the report so they are not missed.

### Requests

Manage `XQ` and supported normal-mode `RP` requests. Pattern presets populate a
known theta/phi grid; changing a populated field creates a visible draft that is
committed with **Apply Pattern Changes**. Deleting a pattern first dims and marks its
row; Apply commits the deletion, while Restore or discarding page edits keeps it.

| Preset | What it computes |
|---|---|
| Full 3D pattern | Every elevation and azimuth on a 5° grid. **Add Pattern** starts here. |
| Azimuth cut | A top view: every azimuth, 1° apart, at one elevation. Enter it in **At elevation**. |
| Elevation cut | A side view: every elevation, 1° apart, through one azimuth and the azimuth behind it. Enter it in **At azimuth**. |
| Custom RP grid | Any other theta/phi range, edited directly. |

When the model has ground, the presets stop at the horizon, since nothing radiates
below it. Next to **At elevation** the editor shows the NEC angle it writes: NEC θ is
measured down from straight up, so El 10° is θ 80°. The Theta and Phi fields below
always show the NEC values.

**Choosing the elevation for an azimuth cut.** In free space the main beam is usually
at the horizon (El 0°). Over ground it lifts off: the lowest lobe of a horizontal
antenna sits at about sin(El) ≈ λ / (4 × height), roughly 30° at ½ λ up, 14° at 1 λ
and 7° at 2 λ. You can't know the exact takeoff angle before running, so the usual
workflow is two steps:

1. Run the Full 3D pattern (or an elevation cut) and read its sampled peak, for
   example "12.960 dBi @ El 10°, Az 0° (5° grid)" on Home.
2. Click **Add Cuts Through Peak**. It adds a 1° azimuth cut at the peak's elevation
   and a 1° elevation cut at the peak's azimuth, crossing at the peak. Run again for
   smooth, finely sampled cuts.

After a run, the Azimuth cut preset also starts at the last sampled peak's elevation.
Before any run it starts at El 10° over ground and El 0° in free space. Add Cuts
Through Peak waits until a pending pattern edit is applied or discarded, and skips a
cut that already exists.

Pattern frequencies may follow the model sweep, use one frequency, use an explicit
list, or use a custom continuous range. Explicit pattern frequencies do not replace
the model sweep used for impedance and currents. Workbench generates the necessary
temporary solver sequence and archives it as `model.nec`.

Opening a model initializes this section from its `FR` card and selects **Use Model
FR Frequencies**. Changes remain in place while that model is open, including across
tab changes and model rechecks, but are reset from `FR` when another model is opened.
Use **Reset from Model FR** to restore this behavior without reopening the model.

For a single-frequency `FR` card, the increment may be omitted. Workbench treats it
as zero without reporting a finding and writes an explicit zero only in the generated
solver deck. Multi-frequency sweeps still require a positive increment.

Workbench also accepts original NEC free-format conventions, including comma field
separators, leading `+` signs, two-character card mnemonics followed immediately by
text, a leading UTF-8 byte-order mark, and an `FR` frequency count of zero as the
single-frequency default. Authored formatting is preserved when the source is not
explicitly edited.

Radiation calculations can dominate run time. Request only the angular and frequency
coverage needed for the intended result.

Optimization candidates retain their generated deck and solver artifacts. If a
candidate backend fails, its status and tooltip show the first solver message and
the complete merged solver output is saved as `solver.log` in that candidate's run
folder.

### Running

Run Analysis validates the current source, generates a numeric solver deck, creates
a unique run directory, and starts the solver asynchronously. The Run Monitor shows
the command, output, elapsed time, and final status. Successful automatically opened
monitors close; failures remain visible.

Stopping a run requests process termination. Partial output may remain for diagnosis,
but an interrupted run is not treated as a completed result.

## Results

Results remain associated with the exact archived solver deck that produced them.
If the active model changes, existing results are marked stale rather than silently
reinterpreted.

### Summary and Numerical Results

Summary identifies model, run, backend, and reference impedance. Numerical views
show feedpoint impedance, SWR, current data, and available directional metrics by
frequency. Plot hover readouts expose exact sampled values.

After a completed analysis, Workbench automatically selects the previous completed run
of the same saved model as a historical overlay, but overlays start hidden. The
Impedance and Radiation windows each have an independent **Show Historical Overlay**
checkbox and **Choose Overlay…** menu. The selected run is shared, while visibility
is local to each window. **Compatible**, **Partial**, or **No matching data** indicates
whether that window can compare all, some, or none of its current data. Manual choices
may come from another antenna; Workbench only draws genuinely matching frequencies,
feedpoints, cuts, and sample grids. Use **Clear Overlay** to remove the selection.

### Currents

The Currents window has three tabs. **Antenna** draws the segment currents on a 2D
view of the solved antenna; **Along Wire** plots magnitude along connected wire paths;
**Table** lists every segment.

On the Antenna tab, **Show** selects:

- **Magnitude** — a curve beside each wire whose distance from the wire shows how
  strong the current gets.
- **Phase** — wire color shows phase relative to the feed current on a circular
  scale, so −180° and +180° share a color. Segments carrying under 5% of the peak
  current are grey, because their phase is mostly numerical noise.
- **Instantaneous** — the actual current at one moment of the RF cycle. The side of
  the wire shows which way the current flows at that moment. Drag **Cycle** or press
  **Play** to watch it change; the magnitude envelope stays faintly visible. At 0° the
  feed current is at its positive peak.

**View** chooses the plane; Auto picks the one the antenna mostly lies in. Scroll to
zoom, drag to pan, and double-click to fit. **Scale** enlarges the curves, which are
otherwise sized so neighboring elements do not overlap. Hover a segment for its wire,
segment, current, and phase.

NEC reports current flowing from wire end 1 to end 2, so two parallel elements
entered in opposite directions appear 180° apart even when their physical currents
agree. The Antenna tab and the table's **Phase vs. feed** column therefore treat
straight wires as flowing toward +X, +Y, or +Z, and measure phase from the feed
current, so the feed segment reads 0° and other elements read as leading or lagging
it. The table's **NEC phase** column keeps the solver's own convention: current from end 1 to
end 2, measured against the source voltage. With several sources, the first source
NEC reports is the reference; with none, the column is labeled **Phase, common
direction** and only the direction rule applies. Curved wires keep NEC's direction.

Several wires may share a tag number; tag 0 is common in imported decks. The table's
**Segment** column counts within each wire, and **NEC segment** is the solver's own
number, counted through all wires, which tells such wires apart. The Antenna tab, Along
Wire, and the hover readout place every segment on its own wire. Model Check follows
NEC's rule for `EX`, `LD`, and `TL` references: a segment number counts through every
wire with that tag in deck order, and tag 0 means an absolute segment number.

### Radiation

The radiation frequency selector includes only frequencies with pattern data.

- **2D Pattern** switches between elevation cuts (a side view at one azimuth) and
  azimuth cuts (a top view at one elevation) with **Show Azimuth Cut** / **Show
  Elevation Cut**, cycles available angles,
  reports peak and interpolated 3 dB beamwidth when computable, and probes nearby
  samples under the pointer. Each RP request is a separate **Pattern dataset**. The
  Show … Cut button moves to the cut that crosses the current one at its maximum. When
  the current dataset holds only one kind of cut, such as a 1° azimuth cut, it switches
  to the dataset whose crossing cut has the most samples, so the two cuts from **Add
  Cuts Through Peak** flip back and forth as a pair. The button's tooltip names the
  dataset it will use, or says why none qualifies, and after a switch the summary
  starts with "Switched to RP 3 …, crossing the previous cut's peak" until the next
  change. An azimuth cut needs at least three azimuths, so an elevation-cut dataset
  (just 0° and 180° at each elevation) offers no azimuth cuts of its own. **Go to
  Peak** moves the current cut to the dataset peak, keeping the kind of cut: an azimuth
  cut moves to the peak's elevation, an elevation cut to its azimuth. Every cut says
  which cut it is ("Elevation cut at Az 0°", "Azimuth cut at El 10°") and labels its
  strongest sample "Peak in this cut". When other RP data at the same frequency has a
  stronger sample, the summary adds "Sampled peak (all RP data) …, outside this cut". In Normalized mode the plot and the hover readout state the
  reference level, for example "0 dB = 7.410 dBi". Elevation cuts are labelled in
  elevation, with the horizon level and the cut's two azimuths marked at each side;
  when the pattern has nothing below the horizon (a model over ground) the plot is the
  upper semicircle. Azimuth cuts are seen from above, with Az 0° (+X) on the right and
  azimuth increasing counterclockwise toward +Y, as in EZNEC. A compatible historical overlay is drawn as a muted dashed
  trace. Workbench requires the same frequency, cut orientation/plane, and angular
  sample grid; it reports the historical overlay as unavailable rather than interpolating.
  Normalized mode normalizes each trace to its own peak for shape comparison;
  Absolute mode places both traces on the same dBi scale.
- **3D Pattern** renders the available theta/phi mesh with antenna and current
  overlays. A compatible historical grid appears as a muted dashed wireframe. It
  does not invent missing angular coverage.
- **Performance vs Frequency** shows available forward gain, F/B, and F/R metrics,
  with compatible historical curves shown dashed. The line above the plot reads
  "Gain toward El 15°, Az 45°: 5.900 dBi" for the selected frequency. Right-click a
  sample on the 2D or 3D pattern and choose **Use This Sample as Forward Direction** to
  set the forward elevation and azimuth from it.

Gains are shown as "6.670 dBi @ El 0°, Az 0°": elevation is measured above the horizon
(NEC θ = 90° − El) and azimuth is NEC φ. Tooltips give the NEC θ/φ, frequency, and
component, and a component other than Total is named ("dBi vertical").

Every peak is a calculated sample, so its precision depends on the RP step around it:

| RP step around the peak | Shown as | Meaning |
|---|---|---|
| 2° or finer | `12.960 dBi @ El 10°, Az 0°` | Close to the true peak. |
| over 2°, up to 5° | `… @ El 10°, Az 0° (5° grid)` | Good for locating the lobe; the true peak may be a degree or two away and a few tenths of a dB higher. |
| over 5° | `… (sampled, RP step 10°)` | The true peak may sit well between samples; request a finer RP. |

Azimuth steps are judged by their true spacing, which shrinks toward the zenith: a 10°
azimuth step at El 80° is only about 1.7° apart, so a high-angle peak is not flagged
for it. The note still quotes the RP step as entered.

**Add Cuts Through Peak** (Analysis › Requests) adds 1° cuts through the peak. After
the next run the finer sample wins and the note disappears.

**Which peak is which?** Each label says how much of the pattern it searched:

| Label | Where | Searches |
|---|---|---|
| Peak in this cut | 2D Pattern | the cut on screen |
| Dataset peak | 3D Pattern; **Go to Peak** | the selected RP dataset |
| Sampled peak (all RP data) | 2D Pattern note; **Use Sampled Peak** | every RP dataset at the selected frequency |
| Peak at this frequency | Home | every RP dataset at the selected frequency |
| Peak across the band | Home | every frequency |
| Sampled peak (5° grid) | Optimizer **Find Sampled Peak** | a temporary 5° grid at one frequency |

F/B compares the peak with the same elevation angle turned 180° in azimuth, as other
amateur modeling programs do, so an elevated beam over ground still has an F/B. If the
pattern does not contain that sample, F/B is unavailable rather than substituted with
minimum gain. See [Directional Metrics](optimization-math.md#directional-metrics). F/R
compares the front with the strongest sample at the same elevation and more than 90°
away in azimuth; it is not a full rear-hemisphere search.

### Runs and Historical Review

Run History is discovered from durable run directories and opens as one reusable
window. Double-clicking a row opens one reusable read-only Run Review window. Neither
window replaces the active editor.

Use **Use as Historical Overlay** to select a completed analysis or quick sweep for
comparison. Then enable it in the Impedance or Radiation result window where it is
useful. The overlay remains labeled and never becomes the active result or model.

The live result windows compare one historical overlay at a time. Comparing several
runs belongs in a separate comparison workflow and is not performed implicitly.

Use **Edit Run Note…**, or double-click the Note cell, to record what changed or why
a run matters. Notes are stored only in run metadata; they do not alter archived NEC
input or solver output. Overlay labels use the note when one is available.

Run Review can display the authored source and generated solver deck. Use **Open
Snapshot as New Model** only when you intentionally want an archived deck in the editor.

Optimization candidates and validation steps are grouped under their parent session
instead of appearing as unrelated ordinary runs.

### Run Artifacts

A normal run directory contains at least:

- `model.source.nec` — authored source
- `model.nec` — generated numeric solver input
- `model.out` — solver output
- `run.log` and JSON metadata

These files provide reproducibility and should not be confused with the currently
open model.

## Optimize

Optimization consumes existing parameters. It does not decide which NEC fields a
parameter controls. Use **Choose Parameter Fields…** to jump to Structured Cards
when another field must be parameterized.

### Search Methods

- **Parameter Sweep** evaluates evenly spaced values for one parameter. Use it to
  inspect the full response across a known range.
- **Adaptive Optimize** performs bounded coordinate refinements around the current
  best result. It is transparent and efficient for localized improvement.
- **Nelder–Mead** moves a bounded simplex and can adjust interacting continuous
  parameters together.
- **Differential Evolution** uses a seeded bounded population to explore broader,
  multi-modal search spaces.

All methods use the same variables, frequency plan, objective, candidate evaluator,
artifacts, cancellation, and Apply actions. They differ only in candidate proposal
and stopping rules.

### Variables and Bounds

Choose the search **Method** at the top of the Search section, then check the
parameters to search and set **Min**, **Max**, and **Tol.** (parameter tolerance) in
the same table for every method. The table shows up to eight parameters before it
scrolls. Bounds constrain proposed candidates. Parameter tolerance describes
spatial convergence; score tolerance describes objective improvement. They are
different quantities.

Parameter Sweep varies exactly one parameter: checking a row in Sweep mode replaces
the previous choice. The other methods change every checked parameter together.

When the run button is unavailable, the status line under the progress bar states
the first thing that must be fixed. An incomplete objective, such as a directional
goal whose direction has not been set up, is also flagged in red under the Objective
summary.

### Frequencies

Every candidate uses the selected frequency plan:

- the model `FR` sweep
- an explicit editable list
- a custom continuous sweep
- amateur-band convenience values

Explicit lists can cover separated bands without calculating every frequency between
them. The workload summary shows the number of candidate-frequency calculations.

### Objectives

Available criteria include SWR, resistance, reactance, forward gain, F/B, and F/R.
A zero weight disables a criterion. Each enabled criterion selects:

- **Goal** — Minimize, Maximize, Target, Good Enough ≤, or Good Enough ≥
- **Value** — used by Target or Good Enough
- **Band Evaluation** — worst point, average, or best point across the frequency plan
- **Weight** — relative influence on the combined score

The Objective Summary translates the configured rows into plain-language bullets.
Lower combined scores are always better. Contributions are normalized before
weighting so ohms and decibels are not combined as raw, incompatible numbers.

Use **Minimize Worst-Case SWR** when every reduction in the highest SWR should
improve the score. Use **Keep SWR Below Limit** when meeting the limit is sufficient;
all candidates at or below that limit receive zero SWR penalty, allowing other
enabled criteria to decide among them.

Examples:

- Minimize SWR with Worst Point protects the highest SWR in the plan.
- Maximize gain with Worst Point protects the lowest gain in the plan.
- Good Enough SWR ≤ 2 contributes no penalty where the threshold is satisfied.

Directional objectives select a radiation component and one 2D pattern slice:

- **Azimuth** measures around the antenna at a selected elevation.
- **Elevation** measures above the ground at a selected azimuth.
- **Strongest gain in this slice** is the default and follows the strongest
  sampled lobe for every candidate and frequency.
- **Gain at a specific angle** evaluates one direction and prevents the
  optimizer from switching between lobes.

Use **Choose…** beside **Directional setup** to open the temporary direction
dialog. It shows one selected pattern preview without adding another permanent
panel to the optimization workspace. If a prior multi-angle pattern is
available, Workbench initially fills the slice from its maximum-gain direction.

Tracked cuts use 5-degree samples. They can follow a lobe as geometry changes, but
are more expensive than a specified direction and may switch between competing
lobes. Candidate frequency details show the elevation and azimuth actually selected
(NEC θ/φ in the cell tooltip). F/B
compares the front with the same elevation turned 180° in azimuth; F/R uses the
strongest sample at the lobe's elevation more than 90° away in azimuth, the same rule
as the Radiation window. An elevation cut has no azimuth spread, so there F/R uses the
back half of the cut. **F/R rear region** can switch to **F/R (3D rear hemisphere)**,
which takes the strongest sample more than 90° from the front at any elevation and adds
a 5° grid to every candidate run so those samples exist. See
[Directional Metrics](optimization-math.md#directional-metrics) for the exact definitions.

**Find Sampled Peak (5° grid)…** runs a temporary one-frequency, 5-degree radiation grid
at the first optimization frequency and uses its sampled peak to seed the default slice.
The true peak may lie between grid points; tracked cuts still find the strongest
sample in every candidate.
It does not start optimization or modify the authored NEC model. Over ground it
searches the upper hemisphere; in free space it searches the full sphere. The
button changes to **Update Sampled Peak…** after a usable result and reports when that
direction becomes stale after a model change.

An ordinary active-model run with a multi-angle radiation pattern can provide the
same seed. **Use Latest Sampled Peak** then copies the peak direction from the
latest active-model pattern nearest the first optimization frequency.
When a valid latest pattern exists, enabling a tracked directional objective or
selecting a tracked strategy automatically seeds the cut from that pattern's peak.
**Use Latest Peak to Set Cut** manually reseeds it. A prior run is therefore useful
for setup, but tracked-cut modes recalculate the strongest direction for every
candidate rather than locking to that initial peak. NEC theta is measured down from
the +Z axis, so theta 90 degrees is the horizon and may be a pattern null over ground.

### Starting Model and Candidate Results

Optimization starts from a solver result for the actual current model. This is the
**starting model** shown in Candidate Results and is called the optimization
baseline internally. If no
compatible result is available, the run button reads **Build Baseline & Optimize**.
Workbench first runs only the study frequencies and focused outputs required by the
enabled objective, then starts the selected search after that baseline succeeds.
This internal optimization run leaves the live result windows unchanged and keeps Optimize in
front. Use **Run Analysis**—or **Apply This Candidate and Run**—to run the authored
deck requests and populate the live result windows.

A baseline is compatible only when its solver-effective model, backend, reference
impedance, required frequencies, and radiation coverage match the study. Directional
objectives require the same theta/phi cut grid used by their candidates; a broader
but different authored pattern is not silently substituted. Comment, formatting,
and temporary request changes do not by themselves stale the model fingerprint. The
baseline status explains when another run is required.

The candidate table and objective plot retain the evaluated values and score
breakdown. The winning row is selected and centered when a study completes.
Double-click a candidate to open **Candidate Results**. Candidate-only
SWR/impedance plots are shown first. The Results view also contains the 2D pattern,
**Directional vs Frequency** for Gain/F/B/F/R when available, and Objective Details;
the numeric frequency table remains beside Results. This is temporary optimization
data, not the active model's live results.

Enable **Compare with starting model** only when a comparison is wanted. Candidate
traces remain solid and starting-model traces are dashed on SWR/impedance, 2D pattern,
and Directional vs Frequency views; Objective Details then adds
candidate-minus-starting-model deltas for enabled criteria. This compares one
candidate with the model recorded before the search, not multiple candidates. The
live result windows remain reserved for normal analysis runs and manually selected
historical overlays.

The candidate menu can apply a scored candidate directly to the model or apply it
and start a normal analysis. The existing **Apply Best to Model** action remains the
shortcut for the winning row. After evaluation stops, click any numeric column heading from
Value through F/R to sort the table; Status and Run remain informational. Gain,
F/B, and F/R initially sort highest-first, while the other numeric columns sort
lowest-first. Row tooltips retain the full objective score to six decimal places
when several displayed three-decimal scores appear tied.

If a required directional result is missing, the candidate remains unscored and
the status identifies the affected frequency and direction. Valid impedance
measurements remain available for inspection. Workbench distinguishes absent
pattern coverage from a solver-reported pattern null (`-999.99 dBi`).

Candidate measurements remain optimization data until explicitly promoted:

- **Apply Best to Model** writes the winning values into active `SY` definitions.
- **Apply This Candidate to Model** applies a manually selected candidate.
- **Apply This Candidate and Run** applies it and starts a normal analysis.

Apply is one undoable source edit. It does not alter archived candidate artifacts.

### Choosing a Method

- Start with Parameter Sweep for one variable or to understand a range.
- Use Nelder–Mead for a small, smooth, interacting parameter set.
- Use Differential Evolution when local minima are likely or the initial region is uncertain.
- Use Adaptive Optimize when you want a simple, inspectable coarse-to-fine search.

Always inspect the winning model, candidate curves, bounds, validation findings,
and solver output. An optimizer finds a better score under the stated model and
objective; it does not establish physical validity or construction tolerance.

## Parameterized Source Example

```text
SY frequency=7.1, halfLength=10.03
SY segments=41, feedSegment=(segments+1)/2
GW 1 segments -halfLength 0 10 halfLength 0 10 0.001
FR 0 1 0 0 frequency 0
```

Names are case-insensitive and must be declared before use. Expressions support
parentheses and `+`, `-`, `*`, `/`, and `^`. Check Model reports expression errors
on their source lines. See `examples/40m-symbolic-dipole.nec` for a complete model.

## Practical Guidance

- Save the authored model before major edits or long studies.
- Treat warnings as review prompts, not automatic pass/fail judgments.
- Keep pattern requests no larger than needed.
- Confirm that optimization bounds are physically meaningful.
- Re-run the applied finalist as an ordinary analysis.
- Use AGT and segmentation convergence before trusting small performance differences.
