# Architecture

## Current Boundaries

User-facing decimal presentation is centralized in `ui/DisplayFormat.h`. Standard
workspace values use three decimal places, with scientific notation for very small
or very large values. Source decks, solver artifacts, exports, and precision-sensitive
editors are deliberately excluded from presentation rounding.

- `model`: solver-independent semantic antenna objects using SI units
- `nec`: source-deck parsing, preservation, writing, and semantic conversion
- `ui`: Qt Widgets desktop shell; it does not own parsing or model logic
- `analysis`: external-solver command adapters without Qt dependencies

The main window is a modular workbench rather than a source-editor container.
Its persistent navigation switches among Dashboard, Geometry, NEC Source,
Analysis, Results, and Optimize workspaces through one stacked central area.
Before a NEC deck is created or opened, Dashboard shows the welcome screen.
After loading, Dashboard composes four live panels: a basic editor sharing the
authoritative source document, the existing interactive 3D results renderer, a
model summary, and quick solver results. It does not maintain a second NEC copy.

Geometry owns the detailed XY, XZ, YZ, and 3D editing views. NEC Source owns the
full source editor and structured card tables. Analysis owns frequency,
source, ground, loads/lines, solver, and result-request controls. Results owns
summary, grouped run history, impedance tables and plots, currents, nested 2D/3D
radiation, and immutable raw solver output. Optimize provides bounded
single-variable SWR sweeps.
Project remains a global dock. Bottom-tabbed Validation and Solver Output docks
preserve diagnostics and process logs across every workspace.

Results uses one movable content widget rather than duplicated views. A stable
host remains in the main module stack while the same widget is reparented into a
single reusable top-level window. Reattaching or closing that window moves the
widget back, preserving selected tabs, parsed data, render state, and run context.
This task-window model keeps one-screen tabbed operation while allowing Geometry
and Results to be tiled without creating a window per run.

Impedance, Currents, and Radiation use the same composition rule at category
level: a small detachable-panel wrapper reparents each existing live widget into
one reusable category window. Solver updates continue through the original view
pointers, so category popouts add no duplicate parsed results or renderers. The
Dashboard reuses the radiation renderer in overview mode, hiding result controls
and summary chrome while retaining orbit, pan, and zoom interaction.

Validation errors disable Solve and Optimize while warnings do not. Solve runs
validation again immediately before creating solver artifacts. Existing results
remain visible after source edits, but both Dashboard and Results identify them
as stale until a new successful solve is parsed. The refactor relocates and
composes existing widgets; parsing, source writing, selection synchronization,
solver execution, and both 3D renderers retain their prior boundaries.

NEC Source presents Raw Source and Structured Cards over the same authoritative
text document. Structured Cards contains the validated, unit-aware `GW` wire
table plus schema-specific tables for `EX`, `FR`, `GN/GE`, `LD`, `TL`, `RP`, and
`XQ`, plus explicit `GS` scale editing. Each row retains its original source-line mapping. Selecting a structured
row positions the raw editor cursor on that card; editing a field rewrites only
that mapped line through the existing source command, undo, parse, validation,
and synchronization path. Supported families provide safe default Add actions
and selection-aware Delete actions; both are undoable source edits, and new
control cards are inserted before `XQ`/`EN` as appropriate. Unsupported cards
remain untouched and available in Raw Source rather than being coerced into an
unsafe generic schema.

Model validation also enforces NEC section ordering: all `GW` geometry cards
must precede a terminating `GE`, and `GN`, `EX`, `FR`, loads, requests, and other
control cards must follow that boundary. Wire insertion creates a missing `GE`
when necessary, and structured control-card insertion preserves the boundary,
preventing solver-side “GEOMETRY DATA CARD ERROR” failures that field-only
validation cannot detect.

The first structured-card section is an editable `GW` wire table. It validates
tags, segment counts, coordinates, and radii before replacing the mapped source
line. Add, duplicate, delete, and cell edits use the same source-level undo stack
as geometry operations. Raw source remains authoritative and unsupported cards
remain untouched.

Semantic geometry always uses meters internally, while authored NEC geometry
may use meters, centimeters, millimeters, inches, feet, or a custom scale. `GS`
is a first-class ordered geometry operation: it scales coordinates and wire
radii generated before that card, matching NEC behavior. The standard unit
selector normalizes supported numeric `GW` decks to one `GS` immediately before
`GE`; wire editing divides canonical meter values by the effective deck scale
before rewriting source. Symbolic and unsupported geometry is never flattened
automatically merely to change units.

The Model UI can display meters, centimeters, millimeters, inches, or feet
without changing the stored deck or `GS`. Automatic major-grid spacing is chosen in the active display unit
using `1/2/5 × 10ⁿ` engineering steps, so imperial displays use clean foot or inch
labels rather than converted metric intervals. Grid snapping defaults to rounding
the converted interval upward to a clean `1/2/5 × 10ⁿ` value in the new unit, so
`1 ft` becomes `0.5 m` instead of `0.3048 m`. A preserve-physical-interval mode is
also available. Friendly snap spin boxes step both upward and downward through
the same engineering sequence. Geometry Settings
controls this unit-change behavior, automatic or manual major spacing, minor
divisions, grid/axis/label visibility, grid and endpoint snapping, and endpoint
tolerance. Preferences persist between sessions.

The XY, XZ, and YZ geometry views consume the checked semantic model and share
wire selection with each other and the Project dock. Wire
overlays report live cursor coordinates for the two axes represented by each
orthographic plane; model extents remain available internally for Fit Geometry.
endpoints and whole wires can be dragged in any orthographic plane while
preserving hidden coordinates. Grid and nearby-endpoint snapping can be toggled
independently. Accepted moves update the corresponding `GW` source card and
participate in geometry undo/redo. Context menus create, split, and delete wires
through atomic source-deck commands. Wire Properties edits tags, segments,
endpoints, and radius in the active display unit, with optional nominal bare-wire
sizes from 4/0 through 40 AWG. These edits use the same source command path, so
the card table, raw source, plane views, project tree, and undo stack remain
synchronized. The modal dialog includes both precise radius and AWG controls.
The structured GW table also provides a focused 10–30 AWG convenience selector;
it converts the selected nominal gauge to the canonical radius and delegates to
the same wire-edit command. Symbolic GW rows disable this convenience control.

Field-only transforms preserve authored expressions. Automatic Segmentation
replaces only the `GW` segment count and the affected EX, LD, and TL segment
references rather than serializing entire semantic cards. Direct graphical
movement, splitting, and property replacement are blocked for symbolic `GW`
geometry until an explicit symbol-aware editing workflow is available.

The 3D Model Geometry tab uses a lightweight software projection rendered with
Qt Widgets, avoiding an additional OpenGL dependency. It supports orbit, pan,
zoom, fit, isometric reset, world-axis rendering, wire picking, and synchronized
selection and properties. It is intentionally read-only in this checkpoint.

The Analyze workspace exposes three top-level tabs: Model Setup, Solver, and
Requests. Model Setup contains nested Frequency, Ground & Sources and Loads &
Transmission Lines editors, avoiding another top-level workflow step. Its
compact frequency and ground forms share an adjustable horizontal splitter;
source and attachment tables retain full-width editing areas.

The Model Setup editors manage the first supported analysis cards. They edit one
linear or multiplicative `FR` frequency definition and any number of standard
voltage-source `EX 0` cards using magnitude and phase. Source references are
validated against wire tags and segment counts, and feed positions are marked in
all 2D and 3D geometry views. Setup changes rewrite only the managed source line
or insert a canonical card before later control cards; unsupported and additional
cards remain untouched. The shared source command path provides undo/redo and
keeps the raw deck authoritative.

The frequency form defaults to one analysis frequency. Enabling Frequency Sweep
reveals start, requested end, and linear step controls plus an advanced
multiplicative spacing choice. NEC stores start, spacing, and point count rather
than an end value, so the UI calculates the count without exceeding the requested
end and displays the actual final frequency before applying the `FR` card.

Feed markers are selectable geometry objects rather than passive annotations.
Selection synchronizes the 2D and 3D views, Setup source table, and Project tree.
Marker context menus open the Setup editor or delete the source. Wire context
menus can create a default voltage source on the segment nearest the click in
either an orthographic or 3D projection.

Single-source editing uses a modal EX Properties dialog consistent with wire
properties, while Edit in Setup opens the batch source table. Ground setup
manages `GN` together with the corresponding `GE` ground-plane flag in one
undoable source edit. Supported environments are explicit free space, perfect
ground, finite ground using the reflection approximation, and finite ground using
Sommerfeld/Norton, with custom material values or an average-ground preset.

Analysis Solver stores the selected backend family and executable path separately
from the NEC model deck. It currently supports configuration for NEC-2/nec2c,
OpenNEC, NEC-4-compatible installations, and custom adapters. No solver is
bundled. The first executable adapter targets `nec2c`; other choices remain
visible but are reported as not runnable until their command adapters exist.
When NEC-2 is selected with no saved path, the application discovers `nec2c`
from `PATH`.

Analysis Requests manages a canonical `XQ` current/impedance request and a normal
far-field `RP` request with theta and phi sampling controls. Applying requests
updates, inserts, or removes only the managed cards in one undoable source edit.
The readiness summary requires a freshly checked valid model, supported `FR` and
`EX` definitions, at least one result request, and a runnable solver path. Any raw
source edit immediately invalidates readiness until Check Model runs again.
Check Model distinguishes malformed cards from an unfinished deck: syntax,
numeric, and reference failures remain errors, while missing wire geometry,
frequency setup, or a supported voltage source appear as explicit completeness
warnings. This keeps partial models editable without reporting the misleading
"No issues found" state. Its static model-adequacy pass evaluates segment length
at the highest requested frequency, thin-wire segment/diameter ratios,
center-source segmentation, adjoining segment consistency, and large junctions.
These findings are non-blocking because NEC modeling limits require engineering
judgment. The Model Adequacy dock categorizes the findings and keeps source-line
navigation. Solver-backed Average Gain Test is a separate `average-gain-test`
run type rather than an inference from static geometry. It archives the authored
source, resolves symbols, generates a one-frequency lossless deck with the
appropriate whole-sphere or perfect-ground hemisphere `RP` request, parses the
solver's average power gain, and stores test metadata in `agt.json`. Historical
AGT rows reopen Results Validation without loading the transformed test deck as
the editable model. Segmentation convergence uses a `convergence-session` parent
with hidden `convergence-step` children. Each child archives a progressively
refined numeric deck while remapping supported EX, LD, and TL references by
relative wire position. Validation compares successive complex impedance and
peak-gain values; selected tolerances describe observed numerical stability,
not a universal correctness pass.

Run Analysis writes the checked source deck to a unique directory under the
application's durable local-data location and starts the solver asynchronously with `QProcess`.
Every analysis, AGT, convergence, and optimization deck passes through one
solver-boundary normalization step. Workbench preserves inline `CM` notes in the
authored source snapshot, while external NEC decks retain only the opening
comment block so strict NEC-2 implementations do not interpret later comments
as geometry cards.
The Results workspace's Run History tab records status, duration, backend, and artifact location. Standard
output, standard error, and the completed NEC output file are mirrored to the
Results Raw NEC Output tab and Solver Output dock; `model.nec`, `model.out`, and
`run.log` remain in the run directory. Runs support a configurable timeout and
manual cancellation. Each directory includes versioned JSON metadata; records
are discovered on startup, incomplete records are marked Interrupted, and
using View Run Results or double-clicking a historical row reloads its log,
result tables, and plots without replacing the editable document. The archived
`model.nec` is parsed separately for result geometry and radiation overlays. A
persistent result-context banner distinguishes historical output from the active
model, while explicit actions inspect the archived deck read-only or open it as
an untitled editable model. Historical optimization and convergence sessions use
the same return context, expose read-only session banners, and cannot re-enable
execution against the current editor. Parsing raw
NEC output into structured result objects is a
separate core layer. The first parser reads each frequency block's antenna-input
rows into backend-neutral feedpoint results. Results Numerical Results shows
frequency, source location, resistance, reactance, impedance
magnitude and phase, input power, and SWR referenced to 50 ohms. Raw output
remains available for audit and future parsers.

Results Sweep Plots renders resistance and reactance together and SWR in a
separate vertically resizable plot. The Qt Widgets renderer has no charting
dependency; it handles automatic axes, single-frequency markers, multiple
feedpoints, legends, and exact hover readouts while retaining the tabular view.

The output parser also reads each frequency block's segment-current table and
far-field radiation samples. Results Currents provides magnitude-versus-
segment plots plus complex-current tables. Radiation 2D selects frequency and
either a vertical phi plane or horizontal theta angle for a normalized polar
gain cut. Left/Right cycle the available angles and Space switches orientation;
matching buttons expose the same controls. A complete vertical cut combines
opposing phi planes, so older single-plane runs must be rerun with the updated
Elevation Cut preset. Polar plots use a logarithmic relative-dB radial scale,
label 30-degree spokes and 10 dB rings, and report the nearest sampled angle,
absolute dBi, and relative dB under the pointer. Radiation 3D renders the available theta/phi samples as
an orbitable, mouse-wheel-zoomable normalized wireframe mesh and
overlays the antenna geometry from the exact run deck at its center. The mesh
connects both constant-theta and constant-phi sample directions and closes the
azimuth rings when full coverage exists. Analysis Requests provides explicit 2D
Elevation Cut and Full 3D Pattern presets; the latter requests theta 0–180° and
phi 0–350°. Older or custom runs containing fewer than three phi planes remain
partial and display a coverage warning rather than inventing symmetry. Gain
values more than 40 dB below the pattern peak are clipped for readable geometry;
the parsed dB values remain unchanged.

The shared 3D Results renderer layers the exact run antenna, segment-current
magnitude, and radiation surface in one projection. Each layer can be hidden
independently. Current segments use a blue-to-red magnitude scale and expose
wire, segment, amperes, and phase under the pointer. If radiation is hidden or
unavailable, the antenna automatically expands to use the viewport. Keeping
these as renderer layers rather than separate widgets allows later workspace
layout changes without duplicating data or rendering code.

The status bar identifies the currently open NEC file in every workspace. Each
run record also stores its originating file path. Historical result summaries
show the model filename, run timestamp, and backend. Viewing a run leaves the
active editor untouched; opening its archived input as an untitled editable copy
requires the separate Open Snapshot as New Model action. Immutable run artifacts
are never overwritten. Legacy run records without source metadata are labeled
as archived `model.nec` snapshots.

Model Setup → Loads & Transmission Lines provides structured editable tables for NEC `LD` types 0–5
and `TL` cards. Changes rewrite or insert one canonical card through the shared
undoable source path. Validation checks numeric field types, load wire/segment
ranges, and both transmission-line endpoints. Supported attachments participate
in automatic segmentation through normalized wire-position remapping.

## Automatic Segmentation

Segmentation is an explicit Model tool with a preview, not an invisible mutation
performed when Run Analysis is clicked. It uses the highest requested frequency
and a user-selected 5–100 segments per wavelength, then shows each wire's old
and proposed count and resulting segment length. Applying the proposal updates
all affected `GW` cards in one undoable source edit. An optional odd-count policy
keeps an unambiguous center segment on excited wires.

Supported `EX 0` sources, `LD` ranges, and `TL` endpoints are remapped by
normalized position along their wires, and the preview reports changed segment
numbers. TL-connected wires participate in the odd-center policy. Unsupported
forms and `NT` network cards remain blocked rather than risk moving an electrical
connection.

`NecDocument` and `AntennaModel` are intentionally distinct. Parsing preserves
every source line, including blank and unknown lines. Semantic conversion reads
supported cards without discarding source it cannot yet interpret.

## Parameterization Core

The Qt-free NEC core recognizes `SY` cards and provides a solver-independent
symbol resolver. Symbol names are case-insensitive, may reference definitions
declared earlier in the deck, and support parentheses plus `+`, `-`, `*`, `/`,
and `^`. Multiple `name=expression` assignments may appear on one `SY` line.
Resolution returns the original definitions, line-specific diagnostics, and a
generated numeric deck with `SY` declarations removed. Comments, unsupported
cards, line-ending style, and retained blank lines are preserved.

Model checking resolves expressions into a line-preserving intermediate source,
so diagnostics and graphical objects still point to the authored lines. Analysis
runs archive the authored source as `model.source.nec` and pass a generated,
numeric `model.nec` to the selected backend. Native NEC solvers therefore do not
need to understand Workbench symbols.

Each retained symbol definition includes its exact authored expression, resolved
numeric value, source line, and whether it is a direct numeric assignment that
the current Parameter Sweep may adjust. Raw SY values are unit-neutral. Geometry units and `GS` scaling apply
where expressions are consumed by NEC cards; they are not inferred back onto a
symbol merely because its name appears in a geometry field.

Raw source is currently authoritative for symbolic fields. Structured or
graphical edits may replace an expression with its current numeric value. The
Optimize workspace can inspect definitions and override one value across a
bounded linear sweep without modifying the authored source.

Optimization candidates use the same external solver adapter and durable run
store as ordinary analysis. Objective evaluation is centralized in the core
analysis layer rather than embedded in solver lifecycle code. The initial
objectives minimize either the maximum SWR across all returned feedpoint
frequencies or the SWR nearest a selected frequency. Candidate input removes
`RP` requests and ensures an `XQ` request, avoiding unnecessary far-field
calculations. Optimization can retain the model's `FR` sweep or replace it with
an explicit, sorted frequency set. Explicit sets are emitted as repeated
single-frequency `FR`/`XQ` blocks, which permits disconnected bands in one
candidate process. Parsed feedpoint rows remain attached to each candidate so
the workspace can show its full frequency-by-frequency SWR and impedance detail.
An `optimization.json` artifact records the variable, value, objective,
frequency mode and points, selected objective frequency, and reference impedance
used for each generated numeric deck.

Run metadata distinguishes ordinary analyses, AGT runs, convergence sessions
and steps, and parameter-sweep sessions and candidates. The Results run browser
hides child rows and shows
their parent session instead; opening the session reconstructs the candidate
table from durable child artifacts. Deleting the session removes the group.

## Near-Term Milestones

1. Add candidate plots and apply-best workflow.
2. Add gain and pattern objectives, constraints, and bounded variables.
3. Add normalized attachments and optional solver adapters, including OpenNEC.

See [Development Roadmap](roadmap.md) for the broader sequence and parking lot.

Solver executables are never bundled. NEC-4 and NEC-5 support will accept paths
to user-provided licensed executables.
