# Architecture

## Current Boundaries

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

Geometry owns the structured wire-card editor and detailed XY, XZ, YZ, and 3D
editing views. NEC Source owns the full source editor. Analysis owns frequency,
source, ground, loads/lines, solver, result-request, and run-history controls.
Results owns numerical impedance, sweep plots, currents, 2D and 3D radiation,
and raw solver output. Optimize retains its incremental placeholder controls;
no optimizer algorithm is implied by the workspace refactor. Project and
Properties remain global docks. Bottom-tabbed Validation, Solver Output, and
Messages docks preserve diagnostics and process logs across every workspace.

Validation errors disable Solve and Optimize while warnings do not. Solve runs
validation again immediately before creating solver artifacts. Existing results
remain visible after source edits, but both Dashboard and Results identify them
as stale until a new successful solve is parsed. The refactor relocates and
composes existing widgets; parsing, source writing, selection synchronization,
solver execution, and both 3D renderers retain their prior boundaries.

The first structured-card section is an editable `GW` wire table. It validates
tags, segment counts, coordinates, and radii before replacing the mapped source
line. Add, duplicate, delete, and cell edits use the same source-level undo stack
as geometry operations. Raw source remains authoritative and unsupported cards
remain untouched.

NEC source and semantic geometry always use meters internally. The Model UI can
display meters, centimeters, millimeters, inches, or feet without changing the
stored deck. Automatic major-grid spacing is chosen in the active display unit
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
wire selection with each other and the Project and Properties docks. Wire
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
synchronized. The contextual Properties dock also offers AWG as a quick radius
editor, while the modal dialog handles the complete wire definition.

The 3D Model Geometry tab uses a lightweight software projection rendered with
Qt Widgets, avoiding an additional OpenGL dependency. It supports orbit, pan,
zoom, fit, isometric reset, world-axis rendering, wire picking, and synchronized
selection and properties. It is intentionally read-only in this checkpoint.

The Model Setup tab manages the first supported analysis cards. It edits one
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
Selection synchronizes the 2D and 3D views, Setup source table, Project tree, and
contextual Properties dock. Marker context menus open the Setup editor or delete
the source. Wire context menus can create a default voltage source on the segment
nearest the click in either an orthographic or 3D projection.

Single-source editing uses a modal EX Properties dialog consistent with wire
properties, while Edit in Setup opens the batch source table. Ground setup
manages `GN` together with the corresponding `GE` ground-plane flag in one
undoable source edit. Supported environments are explicit free space, perfect
ground, finite ground using the reflection approximation, and finite ground using
Sommerfeld/Norton, with custom material values or an average-ground preset.

Analyze Setup stores the selected backend family and executable path separately
from the NEC model deck. It currently supports configuration for NEC-2/nec2c,
OpenNEC, NEC-4-compatible installations, and custom adapters. No solver is
bundled. The first executable adapter targets `nec2c`; other choices remain
visible but are reported as not runnable until their command adapters exist.
When NEC-2 is selected with no saved path, the application discovers `nec2c`
from `PATH`.

Analyze Requests manages a canonical `XQ` current/impedance request and a normal
far-field `RP` request with theta and phi sampling controls. Applying requests
updates, inserts, or removes only the managed cards in one undoable source edit.
The readiness summary requires a freshly checked valid model, supported `FR` and
`EX` definitions, at least one result request, and a runnable solver path. Any raw
source edit immediately invalidates readiness until Check Model runs again.
Check Model distinguishes malformed cards from an unfinished deck: syntax,
numeric, and reference failures remain errors, while missing wire geometry,
frequency setup, or a supported voltage source appear as explicit completeness
warnings. This keeps partial models editable without reporting the misleading
“No issues found” state.

Run Analysis writes the checked source deck to a unique directory under the
application's durable local-data location and starts the solver asynchronously with `QProcess`.
The Runs tab records status, duration, backend, and artifact location. Standard
output, standard error, and the completed NEC output file are mirrored to the
Analyze Output tab and Solver Output dock; `model.nec`, `model.out`, and
`run.log` remain in the run directory. Runs support a configurable timeout and
manual cancellation. Each directory includes versioned JSON metadata; records
are discovered on startup, incomplete records are marked Interrupted, and
selecting a historical row reloads its log, result tables, and plots. Parsing raw
NEC output into structured result objects is a
separate core layer. The first parser reads each frequency block's antenna-input
rows into backend-neutral feedpoint results. Analyze Results and Visualize
Impedance show frequency, source location, resistance, reactance, impedance
magnitude and phase, input power, and SWR referenced to 50 ohms. Raw output
remains available for audit and future parsers.

Visualize Sweep Plots renders resistance and reactance together and SWR in a
separate vertically resizable plot. The Qt Widgets renderer has no charting
dependency; it handles automatic axes, single-frequency markers, multiple
feedpoints, legends, and exact hover readouts while retaining the tabular view.

The output parser also reads each frequency block's segment-current table and
far-field radiation samples. Visualize Currents provides magnitude-versus-
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
azimuth rings when full coverage exists. Analyze Requests provides explicit 2D
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
run record also stores its originating file path, while result summaries and the
solver log show the friendly model filename beside the immutable run ID. Legacy
run records without this metadata are labeled as archived `model.nec` snapshots.

Model Loads & Lines provides structured editable tables for NEC `LD` types 0–5
and `TL` cards. Changes rewrite or insert one canonical card through the shared
undoable source path. Validation checks numeric field types, load wire/segment
ranges, and both transmission-line endpoints. These attachments intentionally
block automatic segmentation until generalized normalized attachment mapping is
implemented.

## Automatic Segmentation

Segmentation is an explicit Model tool with a preview, not an invisible mutation
performed when Run Analysis is clicked. It uses the highest requested frequency
and a user-selected 5–100 segments per wavelength, then shows each wire's old
and proposed count and resulting segment length. Applying the proposal updates
all affected `GW` cards in one undoable source edit. An optional odd-count policy
keeps an unambiguous center segment on excited wires.

Supported `EX 0` sources are remapped by normalized position along their wire and
the preview reports changed segment numbers. Decks containing unsupported EX
types or `LD`, `TL`, or `NT` attachments are blocked rather than risk moving an
electrical connection. A persistent normalized attachment model is still needed
before those card families can participate safely.

`NecDocument` and `AntennaModel` are intentionally distinct. Parsing preserves
every source line, including blank and unknown lines. Semantic conversion reads
supported cards without discarding source it cannot yet interpret.

## Near-Term Milestones

1. Add normalized attachments for `LD`, `TL`, `NT`, and additional EX types.
2. Add radiation polarization and field-component views.
3. Add gain-surface mesh filling and export.
4. Add optional external-solver adapters, including OpenNEC.

Solver executables are never bundled. NEC-4 and NEC-5 support will accept paths
to user-provided licensed executables.
