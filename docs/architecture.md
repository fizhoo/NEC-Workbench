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
Its persistent navigation switches among Home, Model, Analysis, Results, and
Optimize workspaces through one stacked central area. Model contains nested
Geometry and NEC Deck workspaces; NEC Deck contains the existing Raw Source and
Structured Cards views.
Before a NEC deck is created or opened, Dashboard shows the welcome screen.
After loading, Dashboard composes four live panels: a basic editor sharing the
authoritative source document, the existing interactive 3D results renderer, a
model summary, and quick solver results. It does not maintain a second NEC copy.
Workspace tabs provide destination navigation; the loaded-model Dashboard keeps
only Check Model and Run Analysis as global quick actions, right-aligned within
the NEC Source panel. Validation actions remain contextual beside their status
in a compact two-column quality area, while less-frequent modeling operations
stay in the menu system.

Menus and tabs have separate roles: tabs navigate among workspaces, menus expose
the complete command inventory, and the main toolbar contains only frequent
global actions. The Run menu owns standard analysis, temporary Quick Frequency
Sweep, and Stop commands. Stop
delegates to the active ordinary solver, convergence study, or parameter sweep.
Reset Layout affects only window/dock placement and never model or result data.

Model → Geometry owns the detailed XY, XZ, YZ, and 3D editing views. Model → NEC
Deck owns the full source editor and structured card tables. Model also owns the
existing source, load/transmission-line, and ground/environment editors. Analysis
owns frequency, solver, and result-request controls. Results owns
summary, grouped run history, impedance tables and plots, currents, nested 2D/3D
radiation, and immutable raw solver output. Optimize provides bounded
single-variable SWR sweeps.
Project remains a global dock and is automatically hidden while Results or Optimize
owns the central workspace, then restored to its prior visibility. Reusable non-modal
Model Check Report and Run Monitor windows preserve diagnostics and live process logs
without reserving permanent space in the main window. Successful automatically opened
run monitors hide after completion; explicit user opening pins the monitor, while
failure states remain visible.

The authoritative `QTextDocument` owns one chronological model-edit history. A thin
metadata layer records human-readable operation names and originating workspaces for
structured transactions while direct typing is labeled as a raw-source edit. Undo and
Redo therefore retain native text semantics while providing contextual action labels,
status feedback, and navigation across model workspaces.

Quick Frequency Sweep uses the same archived-run and result pipeline as a normal
analysis. The Qt dialog produces a `FrequencyDefinition`; the Qt-free SolverInput
layer replaces `FR` and request cards only in the generated numeric deck. The
authored source snapshot remains unchanged, and run metadata identifies the
temporary override.

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
table plus a category tree leading to schema-specific tables for `EX`, `FR`,
`GN/GE`, `LD`, `TL`, `RP`, and `XQ`, plus explicit `SP`, `SM`, and `SC` surface
patch editors, `GS` scale, and `Z0`/`ZO`
reference-impedance editing. Each row retains its original source-line mapping. Selecting a structured
row positions the raw editor cursor on that card; editing a field rewrites only
that mapped line through the canonical source-document transaction, parse,
validation, and synchronization path. Supported families provide safe default Add actions
and selection-aware Delete actions; both are undoable source edits, and new
control cards are inserted before `XQ`/`EN` as appropriate. The Qt-free
`NecCardCatalog` is the source of truth for standard NEC-2 mnemonics, fixed-field
layouts, workspace categories, and support levels. Recognized cards without a
dedicated editor are exposed through generic fixed-field tables; unknown solver
extensions remain untouched and available in Raw Source.

Recognition does not imply graphical expansion. Generated geometry and transformation
cards are categorized and type-checked, but the geometry converter continues to render
only semantics it implements. This boundary prevents the UI from inventing geometry
while allowing card coverage to grow independently from source preservation.

The semantic geometry converter keeps wire paths and surface patches as separate model
primitives. It expands arbitrary and shaped `SP` definitions, linked `SC` patches, and
`SM` grids without fabricating wire tags or segments. Orthographic, 3D, Dashboard, and
radiation-overlay renderers consume those same read-only patch polygons.

The converter applies `GM`, `GX`, `GR`, and `GS` in authored order to both supported
wire and surface geometry.
`GM` supports move-in-place, first-tag selection, successive copies, and tag increments;
`GX` expands requested Z, Y, then X plane reflections using NEC tag-increment rules; and
`GR` treats its count as the total number of azimuthal sectors around Z. Transformed
straight, tapered, arc, and helix paths remain source-mapped to their generating card
and read-only. The authored transform cards, rather than flattened wires, continue to
be sent to the external solver.

Model validation also enforces NEC section ordering: all recognized geometry cards
must precede a terminating `GE`, and `GN`, `EX`, `FR`, loads, requests, and other
control cards must follow that boundary. Wire insertion creates a missing `GE`
when necessary, and structured control-card insertion preserves the boundary,
preventing solver-side “GEOMETRY DATA CARD ERROR” failures that field-only
validation cannot detect.

The first structured-card section is an editable `GW` wire table. It validates
tags, segment counts, coordinates, and radii before replacing the mapped source
line. Add, duplicate, delete, and cell edits use the same source-document history
as geometry operations and direct text edits. Raw source remains authoritative and unsupported cards
remain untouched.

Semantic geometry always uses meters internally, while authored NEC geometry
may use meters, centimeters, millimeters, inches, feet, or a custom scale. `GS`
is a first-class ordered geometry operation: it scales wire coordinates/radii and
surface-patch coordinates generated before that card, matching NEC behavior. The standard unit
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

The XY, XZ, and YZ geometry views consume the checked semantic model, render surface
patches behind wires, and share wire selection with each other and the Project dock. Wire
overlays report live cursor coordinates for the two axes represented by each
orthographic plane; model extents remain available internally for Fit Geometry.
endpoints and whole wires can be dragged in any orthographic plane while
preserving hidden coordinates. Grid and nearby-endpoint snapping can be toggled
independently. Accepted moves update the corresponding `GW` source card as one
atomic source-document transaction. Context menus create, split, and delete wires
through the same transaction path. Wire Properties edits tags, segments,
endpoints, and radius in the active display unit, with optional nominal bare-wire
sizes from 4/0 through 40 AWG. These edits use the same source transaction path, so
the card table, raw source, plane views, project tree, and undo history remain
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

The Analysis workspace exposes Solver, Frequency, and Requests tabs. The former
combined setup editor supplies three synchronized pages: Frequency is hosted by
Analysis, while Sources and Environment are hosted by Model. The existing Loads
& Transmission Lines editor is also hosted by Model. Reparenting these pages
changes navigation ownership without duplicating controls or semantic state.

The model and frequency editors manage the first supported analysis cards. They edit one
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

Analysis Solver stores the executable path separately from the NEC model deck.
Each backend ID owns an independent executable path in `QSettings`, so changing
the selected protocol restores that backend's prior executable without affecting
the deck or another backend's configuration. The previous single-path setting is
migrated to the backend that was selected when it was saved.
The backend ID selects a process protocol rather than inferring behavior from an
executable filename. `nec2c` uses attached `-i`/`-o` arguments, OpenNEC uses a
positional input with `-f original -o`, and 4nec2 NEC2dXS receives two filenames
through process standard input. All three feed the same backend-neutral result
model and durable run store; no solver is bundled.
When NEC-2 is selected with no saved path, the application discovers `nec2c`
from `PATH`; OpenNEC similarly discovers `onec`. NEC2dXS capacity is inferred
from standard executable names only for readiness warnings. A manually selected
larger-capacity executable remains valid and does not change deck segmentation.

Analysis Requests manages a canonical `XQ` current/impedance request and every
supported normal-mode `RP` card through a row-based theta/phi editor. Pattern
add and duplicate actions create drafts; apply and delete update only the selected
card through the undoable source path. Solver-output parsing assigns each
radiation block a frequency-local dataset index so 2D cuts and 3D grids remain
selectable rather than being merged.
The global Results frequency drives summary, impedance, current, and raw-output
navigation. Radiation views maintain a synchronized category-local selector
populated exclusively from RP-bearing frequencies, preventing impedance-only FR
points from crowding or misleading the pattern controls.
For a model-FR radiation request, solver preparation preserves the
model's native `FR` sweep and `RP` sequence instead of expanding it into one
command pair per frequency. Single-frequency, explicit-list, and custom-range
policies use the shared frequency-plan expansion and create explicit
one-frequency `FR` plus `RP` request blocks while retaining the original sweep
for impedance and SWR. This keeps large model-wide sweeps compact while allowing
independent control over expensive radiation calculations.
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
judgment. The Model Check Report categorizes the findings and keeps source-line
navigation. Solver-backed Average Gain Test is a separate `average-gain-test`
run type rather than an inference from static geometry. It archives the authored
source, resolves symbols, generates a one-frequency lossless deck with the
appropriate whole-sphere or perfect-ground hemisphere `RP` request, parses the
solver's average power gain, and stores test metadata in `agt.json`. If the
backend omits that summary but returns the complete requested grid, the Qt-free
analysis layer integrates linear total gain over spherical cells and derives the
average and solid angle. Historical
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
Results Raw NEC Output tab and Run Monitor; `model.nec`, `model.out`, and
`run.log` remain in the run directory. Runs support a configurable timeout and
manual cancellation. Each directory includes versioned JSON metadata; records
are discovered on startup, incomplete records are marked Interrupted, and
using Open Run Review or double-clicking a historical row loads one reusable,
top-level review window without replacing the editable document or the active
Results workspace. The review window owns separate view instances but reuses the
same parsing, plotting, optimization-session, and convergence-session components.
It never includes Run History itself. The archived `model.nec` is parsed separately
for result geometry and radiation overlays. Its Summary presents `model.source.nec`
and the generated `model.nec` in a read-only split panel, while an explicit action
can open the solver snapshot as a new editable model. Historical optimization and
convergence sessions expose read-only session banners and cannot re-enable execution
against the current editor. Parsing raw
NEC output into structured result objects is a
separate core layer. The first parser reads each frequency block's antenna-input
rows into backend-neutral feedpoint results. Results Numerical Results shows
frequency, source location, resistance, reactance, impedance
magnitude and phase, input power, and SWR referenced to the model's `Z0`/`ZO`
value, defaulting to 50 ohms. `Z0` is the canonical xnec2c-compatible spelling;
legacy `ZO` is accepted. Both remain in authored source but are stripped from
temporary standard-NEC-2 solver decks because `nec2c` does not implement that
extension. Raw output remains available for audit and future parsers.

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
an orbitable, mouse-wheel-zoomable gain-colored wireframe mesh and
overlays the antenna geometry from the exact run deck at its center. The mesh
connects both constant-theta and constant-phi sample directions and closes the
azimuth rings when full coverage exists. Analysis Requests provides explicit 2D
Elevation Cut and Full 3D Pattern presets; the latter requests theta 0–180° and
phi 0–350°. Older or custom runs containing fewer than three phi planes remain
partial and display a coverage warning rather than inventing symmetry. Gain
values more than 40 dB below the pattern peak are clipped for readable geometry;
the parsed dB values remain unchanged. A labeled color scale follows the selected
normalized or absolute display mode. Pointer probing projects the existing NEC
sample nodes through the same camera transform, highlights the nearest projected
node, and reports theta, phi, absolute dBi, and relative dB without interpolation.

The shared 3D Results renderer layers the exact run antenna, segment-current
magnitude, and radiation surface in one projection. Each layer can be hidden
independently. Current segments use a blue-to-red magnitude scale and expose
wire, segment, amperes, and phase under the pointer. If radiation is hidden or
unavailable, the antenna automatically expands to use the viewport. Keeping
these as renderer layers rather than separate widgets allows later workspace
layout changes without duplicating data or rendering code.

`radiationFrequencyMetrics()` is the shared non-widget path for directional
performance across frequency. It finds exact spherical forward and antipodal
samples and the strongest sampled rear-half response using the same definitions
consumed by optimization. Results → Radiation adds one compact Performance vs
Frequency sub-view that converts these domain metrics into the reusable
directional plot widget. Active and historical results therefore share the same
gain/F/B/F/R calculations, extrema summaries, and unavailable-data behavior.

The status bar identifies the currently open NEC file in every workspace. Each
run record also stores its originating file path. Historical result summaries
show the model filename, run timestamp, and backend. Viewing a run leaves the
active editor untouched; opening its archived input as an untitled editable copy
requires the separate Open Snapshot as New Model action. Immutable run artifacts
are never overwritten. Legacy run records without source metadata are labeled
as archived `model.nec` snapshots.

Model → Loads & Transmission Lines provides structured editable tables for NEC `LD` types 0–5
and `TL` cards. Changes rewrite or insert one canonical card through the shared
undoable source path. Validation checks numeric field types, load wire/segment
ranges, conductivity, and both transmission-line endpoints. The LD editor keeps
model values in NEC SI units while presenting µH, pF, and MS/m display units, and
maps its whole-wire scope to zero first/last segment fields. TL rows use model-backed
wire and segment choices, preserve signed characteristic impedance, and convert
the selected Geometry display unit to the meter value required by NEC. Add actions
create local draft rows so incomplete LD or TL definitions do not mutate the source.
Supported attachments participate
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

The semantic `Wire` representation may contain segment-boundary path points and a
geometry kind. This lets `GA`, helical `GH`, and tapered `GW`/`GC` cards participate
in shared projection, fitting, attachment, and adequacy logic without rewriting the
authored cards as many independent `GW` records. Generated paths are marked read-only;
ordinary `GW` wires remain editable through source-backed commands.
Dedicated Structured Cards families edit authored `GA` and `GH` fields directly;
they never flatten generated paths into replacement `GW` cards. Add operations use
the shared source-edit command path and insert geometry before `GE`.

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
numeric value, and source line. Raw SY values are unit-neutral. Geometry units and `GS` scaling apply
where expressions are consumed by NEC cards; they are not inferred back onto a
symbol merely because its name appears in a geometry field.

The Model Parameters editor performs source-level add, update, and delete operations
without introducing a second parameter store. It preserves other assignments when
several definitions share one `SY` line, then runs the normal source check and canonical
source-document Undo/Redo path. The Optimize workspace can inspect definitions and override one value
across a bounded linear sweep without modifying the authored source. Every successfully
resolved SY definition is selectable, including calculated expressions; a candidate
override replaces that definition's resolved value before subsequent definitions and
NEC cards are evaluated.

Optimization candidates use the same external solver adapter and durable run
store as ordinary analysis. A reusable, non-widget `CandidateEvaluator` owns SY
overrides, numeric deck generation, validation, artifact writing, solver process
lifecycle, timeout/cancel handling, output parsing, and objective evaluation.
The equations and algorithm constants implemented by these Qt-free components
are documented in [Optimization Mathematics](optimization-math.md).
Parameter Sweep sequences single-variable candidate requests and renders returned
results; Adaptive Optimize uses the same path while a Qt-free
`AdaptiveVectorSearch` planner chooses bounded coordinate refinements around the
current best parameter vector. A second Qt-free `NelderMeadSearch` planner performs
bounded derivative-free simplex reflection, expansion, contraction, and shrink
steps for one or more variables. A Qt-free `DifferentialEvolutionSearch` planner
provides seeded, bounded DE/rand/1/bin population evolution for broader global
exploration. The optimizer planners own their evaluation-budget or generation-limit,
per-parameter-tolerance, and score-tolerance stopping rules and report an explicit
reason to the workspace and archived session metadata. Future optimizer algorithms
can use the evaluator without duplicating solver logic. A Qt-free `FrequencyPlan` normalizes model sweeps, individual
points, and one or more ranges into sorted, duplicate-free evaluation points.
The shared objective builder combines weighted SWR, resistance, reactance,
explicit-direction forward gain, physical front-to-back, and azimuth-cut
front-to-rear. Each criterion independently stores a goal (Minimize, Maximize,
Target, or directional Good Enough threshold) and a Minimum, Average, or Maximum
frequency reducer. Target criteria reduce absolute error; Good Enough criteria
reduce only threshold violation. This permits robust defaults such as minimizing
maximum SWR and maximizing minimum gain without a global evaluation-mode switch.
One-frequency evaluation is expressed by the shared Frequency Plan rather than a
second objective mode.

Ohmic errors are normalized by reference impedance, directional quantities by
10 dB, and the combined score by total enabled weight. Maximize terms negate the
scaled measurement so the common score remains lower-is-better. The result retains
the raw per-frequency SWR, R, X, forward gain, F/B, and F/R metrics plus minimum,
average, and maximum summaries and extrema frequencies. Candidate tables and
directional plots consume this evaluator-owned data rather than repeating
spherical-direction or objective calculations in the UI. Legacy archived
Minimax, Average, and selected-frequency metadata remains readable and is mapped
onto the corresponding criterion reducers during review. F/B looks up the
sample at the antipodal spherical direction and returns unavailable when that sample
does not exist; minimum pattern gain is never treated as back gain.
F/R subtracts the strongest sample in the rear 180° half of the azimuth cut at
the configured forward theta. The rear region is sampled every 5°, including
both ±90° boundaries; it is intentionally a 2D cut definition rather than a full
rear-hemisphere search.
The evaluator returns the total score and each normalized weighted contribution;
the candidate plot consumes those same values so presentation cannot drift from
ranking behavior. Impedance-only candidate input removes `RP` requests and ensures
an `XQ` request, avoiding unnecessary far-field calculations. A directional
candidate replaces broad model requests with one exact one-point `RP` at the
requested forward direction and, for F/B, one exact one-point `RP` at its physical
opposite for every study frequency. F/R instead adds 37 one-point rear-cut samples
per frequency. This avoids a full angular grid for every
candidate while supporting directional objectives across a frequency plan. Optimization can retain the model's `FR` sweep or replace it with
an explicit, sorted frequency set generated from individual points, ranges, or
both. Explicit sets are emitted as repeated
single-frequency `FR`/`XQ` blocks, which permits disconnected bands in one
candidate process. Parsed feedpoint and directional rows remain attached to each
candidate so the workspace can show full frequency-by-frequency impedance and
directional performance detail.
Applying the best or a selected candidate delegates all selected numeric `SY`
replacements back to `MainWindow` as one source edit so validation and Undo/Redo
remain the only model-mutation mechanism. **Apply This Candidate and Run** uses the
same edit path, rechecks the resulting active model, and then enters the ordinary
analysis runner; it does not promote or mutate the archived candidate artifacts.
An `optimization.json` artifact records the variables, values, frequency mode and
points, reference impedance, criterion weights, per-criterion goals, targets,
Good Enough directions, frequency reducers, forward direction, and polarization component
used for each generated numeric deck. Archived output reconstructs measured gain,
F/B, and F/R columns when a historical optimization session is reopened.

Structured Cards and Model Parameters are two views over one source-backed
parameterization implementation. Structured Cards owns the field-level context menu
that creates or links `SY` expressions; Model Parameters edits the resulting symbol
definitions and explains how to return to the controlling NEC field. No GUI-local
parameter mapping is maintained.

Run metadata distinguishes ordinary analyses, AGT runs, convergence sessions
and steps, and parameter-sweep sessions and candidates. The Results run browser
hides child rows and shows
their parent session instead; opening the session reconstructs the candidate
table from durable child artifacts. Deleting the session removes the group.

## Near-Term Milestones

1. Add finalist sensitivity and construction-tolerance analysis.
2. Extend directional objectives across frequency with weights and constraints.
3. Add normalized attachments and broaden solver-adapter verification.

See [Development Roadmap](roadmap.md) for the broader sequence and parking lot.

Solver executables are never bundled. NEC-4 and NEC-5 support will accept paths
to user-provided licensed executables.

## Pattern-Frequency Solver Decks

The Analysis Requests frequency plan is a run-time policy and does not rewrite the
authored model. Every run preserves that source as `model.source.nec`. When the
model's complete `FR` sweep is selected for patterns, the generated numeric
`model.nec` retains the normalized model request sequence. For a single, selected,
or custom continuous pattern-frequency plan, Workbench removes the generated
deck's original `FR`, `XQ`, and `RP` request cards and emits one combined solver
sequence: the original `FR` definition followed by `XQ` for impedance and currents,
then a one-point `FR` followed by every configured `RP` request for each chosen
pattern frequency. The backend therefore runs once while avoiding radiation
calculations at unselected sweep points.
