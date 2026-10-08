# NEC Workbench User Guide

NEC Workbench organizes work into **Home**, **Model**, **Analysis**, **Results**,
and **Optimize**. Controls provide tooltips; this guide focuses on workflow and
behavior that is not obvious from an individual control.

## Getting Started

1. Create or open a `.nec` model from Home or the File menu.
2. Edit geometry and electrical cards under Model.
3. Configure frequency, solver, and requests under Analysis.
4. Run **Check Model** (`F7`) and resolve blocking errors.
5. Select **Run Analysis**.
6. Review impedance, currents, radiation, raw output, and run history under Results.

NEC Workbench does not include an NEC solver. Configure a separately installed
`nec2c`, OpenNEC, or 4nec2 NEC2dXS executable under **Analysis → Solver**.

## Application Layout

The top workspace bar changes the main task. A compact navigator inside Model,
Analysis, and Results selects a page within that workspace. Collapse the navigator
to icons when screen space is limited.

The menu bar holds less-frequent commands. Important global actions include:

- **Model → Check Model**
- **Model → Automatic Segmentation**
- **Model → Average Gain Test**
- **Model → Segmentation Convergence**
- **Run → Run Analysis**
- **Run → Quick Frequency Sweep**
- **Run → Stop**
- **View → Reset Layout**

Project is hidden automatically in Results and Optimize when more horizontal room
is useful. Results and candidate details may be detached and reused while editing
the model. Selecting their workspace restores an existing hidden or minimized window.

## Home

With no model open, Home provides New, Open, recent files, and documentation.
With a model open, it becomes a dashboard containing source, 3D overview, model
summary, quality status, and quick results.

Home shares the active source and result objects with the other workspaces; it is
not a second copy of the model.

## Model

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

1. Open **Model → NEC Deck → Structured Cards**.
2. Right-click a continuous numeric cell.
3. Create a new parameter or link an existing `SY` definition.

Parameter-controlled cells are marked and retain their source expressions. Integer
and categorical fields are not currently optimization variables. Parameterizing a
negative numeric field creates a positive parameter magnitude and preserves the field
sign in the reference. Linking an existing parameter similarly preserves an equal or
opposite current value, which supports symmetric geometry such as `-half_length` and
`half_length` without changing the model.

### Sources, Loads, Networks, and Environment

These pages provide structured editing for supported `EX`, `LD`, `TL`, `GN`, and
related model setup. Draft rows do not change the source until Apply is selected.
An emphasized Apply button indicates pending changes.

References are validated against available wire tags and segments. Units shown in
the interface are converted to the NEC values stored in source.

### NEC Deck

Raw Source and Structured Cards operate on the same document.

- Use Raw Source for unrestricted deck editing and unsupported extensions.
- Use Structured Cards for labels, choices, validation, and source mapping.
- Double-click an editable cell to change it.
- Right-click a numeric cell to parameterize it.
- Selecting a structured row moves the raw-source cursor to the same card.

Unknown cards and formatting are preserved. Recognition does not imply that every
card has a dedicated editor or graphical representation; see
[NEC Card Support](nec-card-support.md).

### Undo and Redo

Source-backed model changes share one history. Undo may reverse a change made in
another workspace and may navigate to that context. Solver runs, result selection,
window layout, and historical review are not model edits.

## Model Checking and Adequacy

**Check Model** parses source, resolves symbols, rebuilds semantic views, and
reports errors and warnings. Errors block analysis and optimization. Warnings
identify conditions requiring engineering judgment.

Static checks include card ordering, geometry completeness, field types, references,
segment length, thin-wire ratios, source placement, adjoining segment consistency,
and large junctions. Static checks do not prove model accuracy.

### Automatic Segmentation

Automatic Segmentation previews proposed segment counts using the highest modeled
frequency and selected segments per wavelength. Applying the preview is one
undoable model edit. Supported source, load, and transmission-line references are
remapped by relative position; unsupported references block the operation.

### Average Gain Test

Average Gain Test creates a temporary one-frequency, lossless deck in free space
or over perfect ground. It does not modify the authored model. Results are archived
under **Results → Validation**.

### Segmentation Convergence

Convergence runs progressively refined versions of the same model and compares
successive impedance and available gain results. Tolerances describe numerical
stability between levels, not universal model correctness.

## Analysis

### Frequency

Edit the model's supported `FR` definition here. A sweep is represented by start,
spacing, and point count; the displayed final frequency is derived from those values.

**Quick Frequency Sweep** runs a temporary linear or logarithmic sweep without
rewriting the authored `FR` card.

### Solver

Select the backend protocol and executable. Each backend remembers its own path.
The backend selection controls command-line or standard-input behavior; Workbench
does not infer protocol from the executable filename.

### Requests

Manage `XQ` and supported normal-mode `RP` requests. Pattern presets populate a
known theta/phi grid; changing a populated field creates a visible draft that is
committed with **Apply Pattern Changes**. Deleting a pattern first dims and marks its
row; Apply commits the deletion, while Restore or discarding page edits keeps it.

Pattern frequencies may follow the model sweep, use one frequency, use an explicit
list, or use a custom continuous range. Explicit pattern frequencies do not replace
the model sweep used for impedance and currents. Workbench generates the necessary
temporary solver sequence and archives it as `model.nec`.

Radiation calculations can dominate run time. Request only the angular and frequency
coverage needed for the intended result.

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

### Radiation

The radiation frequency selector includes only frequencies with pattern data.

- **2D Pattern** selects vertical or horizontal cuts, cycles available angles,
  reports peak and interpolated 3 dB beamwidth when computable, and probes nearby
  samples under the pointer.
- **3D Pattern** renders the available theta/phi mesh with antenna and current
  overlays. It does not invent missing angular coverage.
- **Performance vs Frequency** shows available forward gain, F/B, and F/R metrics.

F/B requires the physical direction 180° opposite the selected front direction.
If that sample is absent, F/B is unavailable rather than substituted with minimum
gain. F/R uses the documented rear azimuth cut and is not a full rear-hemisphere search.

### Runs and Historical Review

Run History is discovered from durable run directories. Double-clicking a row opens
one reusable read-only Run Review window. It does not replace the active editor.

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

Enable the parameters to search and set Minimum, Maximum, and Tolerance. Bounds
constrain proposed candidates. Parameter tolerance describes spatial convergence;
score tolerance describes objective improvement. They are different quantities.

Parameter Sweep remains single-variable. The other methods support multiple enabled
variables.

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

Examples:

- Minimize SWR with Worst Point protects the highest SWR in the plan.
- Maximize gain with Worst Point protects the lowest gain in the plan.
- Good Enough SWR ≤ 2 contributes no penalty where the threshold is satisfied.

Directional objectives require a forward theta/phi direction and component. They
generate focused radiation samples at every study frequency instead of retaining
broad model `RP` grids. This keeps candidates smaller but is still more expensive
than impedance-only optimization.

### Candidate Review

The candidate table and objective plot retain the evaluated values and score
breakdown. The winning row is selected and centered when a study completes.
Double-click a candidate to inspect its frequency table, SWR/impedance plots, and
available directional plots in one reusable window.

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
