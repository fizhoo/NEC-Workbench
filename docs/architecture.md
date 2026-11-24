# Architecture

NEC Workbench separates authored NEC source, semantic model data, solver
integration, analysis results, and Qt presentation. The central rule is that the
authored NEC document remains authoritative.

## Components

- `model` — solver-independent antenna objects and geometry utilities in SI units
- `nec` — parsing, source preservation, card metadata, validation, symbols, and conversion
- `analysis` — solver input, process protocols, output parsing, validation studies,
  frequency plans, objectives, and optimization searches
- `ui` — Qt Widgets workspaces and renderers

`necwb_core` excludes Qt. The GUI invokes core services rather than duplicating
NEC or optimization behavior in widgets.

## Source and Semantic Model

`NecDocument` preserves source lines, comments, whitespace, line endings, and
unknown extensions. Raw Source and the card tables are two views of this same
document. Structured edits replace only mapped lines and enter the shared
Undo/Redo history.

`AntennaModel` is a checked semantic view used for geometry, attachment markers,
validation, and rendering. It does not replace the source deck. Unsupported
semantics remain preserved in source even when Workbench cannot display them.

Semantic geometry uses meters internally. Display units and grid units are UI
choices. Authored deck scaling, including ordered `GS` operations, remains part
of the model's NEC meaning.

Supported semantic geometry includes ordinary and tapered wires, arcs, helices,
ordered `GM`/`GX`/`GR` transformations, and surface patches. Generated paths and
patches remain read-only graphically so Workbench does not flatten or rewrite the
cards that produced them.

## Editing and History

The main window owns one authoritative `QTextDocument` and one chronological edit
history. Raw typing, structured-card changes, geometry operations, setup changes,
parameter application, and analysis-request edits all pass through the same
source transaction and refresh path.

Human-readable operation metadata supplements Qt's text undo stack (`SourceHistory`,
which also routes `Ctrl+Z` and `Ctrl+Y` to it from any field outside Raw Source). Undo
and Redo may navigate back to the workspace that originated an edit, but never maintain
a second model state.

Symbolic geometry is protected from operations that would replace expressions
with resolved numbers. Field parameterization occurs through the card tables;
Model Parameters edits the resulting `SY` definitions. Optimize consumes those
same definitions.

## Workspaces

The main window is resizable and laid out by `WorkbenchLayout`: one splitter with three
panes. With no model the center shows `StartPanel` (`ui/start`: New, Open, Open Example and
the recent models, kept with their open dates by `recentModels`); at launch `main` asks
`MainWindow::reopenLastModel` to reopen the newest one, guarded by a settings flag that is
set while it opens, so a model that crashed the last launch is skipped. `WorkbenchToolBar` runs
across the top. The left column (`ui/modeltree`) is a `ModelColumn`: the `ModelTree`
(the frequency, ground, requests and Z0 rows with their values, groups with counts, a row
per wire, source, load, line, and network, right-click Add
on the card groups, and the model-quality studies), the `VariablesSection` (a slider row per `SY` variable, each
committed change one `SY` edit; while dragging, `GeometryPreview` redraws the wires from
the resolved symbols at most once a frame, without touching the deck), and the
`PropertiesPane`, which hosts one page for the selection: a group's table from
`CardTables` (the wire table and filtered `StructuredCardEditor`s), which is also the only
editor of each wire, source, load, line, and network (an item selects its row, an arc or
helix its `GA`/`GH` row in Other; a cell edit rewrites only its own field; the line under
the table explains the selected row, and a row's type relabels the headers), the setup
pages, and the Parameters table. The pane holds only
the page on screen, and offers ⇔ whenever that page is wider than the column (every column
of its table, if it has one); ⇔ asks `WorkbenchLayout` to widen the column to the page and
puts it back for a page that fits. The pane applies valid pending edits when the selection
changes and asks only about invalid ones. New EX, LD, TL and NT cards are placed by
`nec::setupCardInsertionIndex`. Every card table hands its number cells to one
`SymbolCells` (`ui/cards`): it shows an expression cell's resolved value and edits it as
written, offers the symbols for the word being typed, owns the Make Variable… / Replace
menu, and sends a typed name or expression to `enterFieldExpression`
(`ui/cards/SymbolPrompt`), which asks under the cell for the value of each new name, adds
its `SY` card with `nec::insertSymbolDefinition`, and writes only that field. The center
shows the geometry editor (`GeometryWorkspace`: a compact toolbar for snapping, units and
settings over either the 2D views in `GeometryPlanes`, which puts `geometry::widestPlane` in
the big view when a model opens and swaps on request, or the 3D view; the big view's title
line holds the Top, Front, Side, 3D, Fit and Currents buttons, which the View menu shares
and which bring the geometry forward from the Deck or a result view, keeping its 2D or 3D
page; Units builds its control per place, so a narrow toolbar's ›› menu gets a working copy;
and each view's counts, grid and cursor go in a thin info line under it, never over the
drawing; the wire, source and load labels go through `LabelPlacer`, which places them after
everything else is drawn, beside their anchor on the side facing the middle of the view,
whole inside it and clear of the markers, the wire-end dots and the labels placed before). In its place the center shows one
of the other views, each a `CenterView` under the same `CenterViewHeader` (← Geometry, Esc):
a full result view, the NEC Deck (raw source), the Average Gain Test, or
Segmentation Convergence. `WorkbenchLayout` keeps them all in one stack, so each keeps its
state while another is shown. The collapsible results column is on the right. The results (`ui/results`) are:

- `ResultsController`, which owns the displayed run, the overlay, the stale flag, and
  the one selected frequency (`ResultState`), and brings everything else up to date.
- `ResultsColumn`: `KeyNumberStrip` (the frequency selector and key numbers), the
  overlay control, four `ResultThumbnail`s, and the More Views list.
- `ResultFullView`: one existing result view with all its controls in the center.
- `LiveSolve`, which solves a one-frequency copy of the deck in a temporary folder after a
  variable changes. Its result reaches the column only (`ResultsController::setLivePoint`):
  the column's state merges it into the run, while `data()`, the full view, exports,
  overlays and the run store keep the last full run.

Thumbnails are compact instances of the existing views (`setCompact`). Every view
receives the same `ResultData`, which groups the radiation samples by frequency once,
so no view copies or rescans the run. `applyResultState` gives a view only what changed
since it was last updated, and the full view brings only the view on screen up to date;
the others catch up when they are shown. The 3D pattern's surface is drawn by
`RadiationSurfaceMesh` with `QPainter`, not OpenGL: one flat-lit patch per θ×φ grid cell in
Turbo colours, sorted far to near with the axis, wire and current lines cut into short
pieces among them, coarser while dragging a dense pattern;
`fitSurfaceView` fits the view to the pattern's projected bounds. `patternPlacement` centres
the pattern on the middle of the antenna's bounding box and sizes it by the antenna's largest
half-dimension, so an antenna raised or moved sideways keeps the same picture, and the fit
covers the pattern and antenna, not the origin. `FlowLayout` wraps a
row of controls that would otherwise widen a narrow view. The **bottom panel** (`BottomPanel`)
along the bottom of the main window holds Problems, the Run log, Runs, and Raw output;
`WorkbenchStatusBar` shows backend, the selected and loaded result frequencies, run state,
and check counts. Menus expose the complete command set.

Besides dialogs, the only windows of their own are the optimizer, its candidate review,
and historical Run Review, which is read-only so opening archived output cannot silently
replace the active editor. `retireOldSettings` runs its steps once per settings store, in
order of `ui/layoutVersion`: version 1 removes what the earlier separate windows and main
window state saved; version 2 drops a saved nec2c or OpenNEC path that is only what the
PATH gives, so the bundled nec2c is found first.

The historical-overlay controller selects at most one archived run
and never changes the active result; the results column's one overlay control decides
whether to display it.

## Validation

Model checking runs after source changes and immediately before a solve. Blocking
syntax, field, ordering, expression, or reference errors prevent analysis and
optimization. Adequacy findings are warnings because their importance depends on
the model and engineering purpose.

Validation commands are grouped in the **Validate** menu. Static findings are listed
in the bottom panel's Problems tab under a note on the check's scope and target solver.
The Average Gain Test result and the segmentation-convergence workspace are center
views opened from their model-tree rows or the menu; the tree's Run starts a study
without changing the center.
The Run log remains execution UI rather than a validation result; durable runs are
reviewed through the Runs tab and the read-only Run Review window.

The static checker covers geometry/control-card ordering, segment length,
thin-wire ratios, source placement, adjoining segmentation, and large junctions.
Average Gain Test and segmentation convergence are solver-backed studies with
their own archived artifacts; they are not inferred from static checks.

Automatic Segmentation is an explicit preview-and-apply operation. It changes
`GW` segment counts and remaps supported `EX`, `LD`, `TL`, and `NT` attachments by
relative wire position in one undoable edit. Unsupported attachment forms block
the operation rather than being guessed. `nec::planSegmentationEdit`
(`src/nec/SegmentationEdit.h`) builds the whole edit from the authored source once:
which wires are skipped because a field they need is symbolic, the edited deck, and
the attachments that deck gives. Marked sources are placed by resolving the edited
deck, the same placement solving uses. The dialog previews that plan and Apply
pushes its source unchanged, so the two cannot disagree.

## Symbols and Solver Decks

Workbench `SY` expressions are solver-independent. The core resolver supports
earlier symbol references, parentheses, and arithmetic operators. A run archives:

- `model.source.nec` — the authored source, including symbols
- `model.nec` — the generated numeric deck sent to the solver

Workbench-only compatibility cards are normalized or removed at this boundary
when required by a strict NEC-2 backend, and so are 4nec2 `'` comments. The active
source is never replaced by the generated deck.

The resolver (`nec/NecSymbolResolver`) requires an expression in a card's NEC integer
fields to come to a whole number, and places voltage sources marked `' necwb:at=P%`
(`nec/SourcePositions`): it converts the resolved geometry, finds the segment at that
point with `model::placeAlongWire`, and writes it into the resolved and generated decks.
Every consumer of a resolution, including optimizer candidates and convergence studies,
therefore sees the same segment. Model Check reports a mark that cannot be placed.
`MainWindow::applyGeometrySource` passes each edit through `refreshSourcePositions`, so
the authored segment field stays current for other programs in the same undo step. The
optimizer finds variables that reach integer fields with `wholeNumberFieldReached` and
allows them only in a Parameter Sweep whose values keep those fields whole.

Quick sweeps and custom pattern-frequency plans also operate at this boundary.
They alter only the generated deck and retain the authored model unchanged.

## Solver Integration

The selected backend identifies a process protocol, not merely an executable name:

- `nec2c` uses attached input/output arguments.
- OpenNEC uses positional input with original-format output options.
- 4nec2 NEC2dXS receives filenames through standard input. Its generated numeric deck
  uses canonical space-delimited fields so direct execution never depends on the full
  4nec2 application's input preprocessing or on unsupported tab separators.

All adapters feed the same run store and backend-neutral result model. Solvers are
always separate processes, so a crash cannot take the application down, `SolverProcessRunner`
can always stop one (terminate, then kill), and several could run at once.

The packages bundle nec2c (packaging/nec2c: the pinned commit and version, `build.sh`,
the MinGW-w64 compat headers, the reference build, the decks and skip list for the result
checks). `build.sh` writes `nec2c-build.txt` beside the executable with the version,
commit, compiler and flags. `ui/SolverLocator` finds each engine's executable afresh at
every start: the path the user chose, else a copy beside the executable or in
`solvers/`, else the PATH. It reads the bundled version from the metadata and asks other
nec2c builds with `-v` in the background, cached by path, size and modification time.
`ui/SolverSettings` is the only writer of the engine settings: it saves the chosen engine
and only paths the user typed or browsed to, never one inside the application folder or a
mounted AppImage. A chosen path that has gone is reported, never replaced. Run records
keep the engine version and path. CI runs the hidden `nec-workbench --package-check`
(throwaway settings, no window) on each unpacked package: it finds the bundled nec2c,
reads its version, solves, and stops long runs with Stop and with a timeout. It also
compares the packaged nec2c's output with a reference build's, as text, and the Windows
build's parsed results with Linux's (`necwb_nec2c_check`).

Solver execution is asynchronous and supports live output, cancellation, and a
timeout. Every run receives a unique durable directory containing source,
generated input, output, logs, and versioned metadata. Startup discovery rebuilds
the Runs list from those directories and marks interrupted records appropriately.

Completed runs also index the metadata needed for optimization baselines: a
normalized solver-model fingerprint, backend, reference impedance, impedance
frequencies, and exact radiation sample-grid fingerprints. The model fingerprint
ignores comments, formatting, and temporary `FR`/`RP`/`XQ` requests while retaining
solver-effective model values. Core compatibility logic distinguishes model,
backend, impedance, frequency, and radiation-coverage mismatches; the optimization
workflow can therefore reject stale or incomplete baselines without relying on a
filename or whichever historical result happens to be displayed.

## Requests and Frequency Plans

The authored model may contain an ordinary `FR` sweep and one or more result
requests. Workbench can either retain that native sequence or generate explicit
single-frequency request groups for selected frequencies or custom ranges.

This permits impedance across a broad sweep while calculating expensive radiation
patterns only where requested. Generated request policy is recorded with the run;
it does not rewrite the authored cards.

Request-frequency state belongs to the active model session. A newly opened model
starts in native `FR` mode and seeds its optional single, selected, and continuous
controls from that deck. Routine rechecks preserve user changes; opening another
model establishes a new request-frequency state.

## Result Model

The output parser produces backend-neutral frequency, feedpoint, current, and
radiation records. Views consume these records rather than reparsing output.

- Impedance and SWR share the model reference impedance (`Z0`/`ZO`, default 50 Ω).
- Current results retain wire, segment, complex current, magnitude, and phase.
  `analysis/SegmentCurrentDisplay` prepares them for drawing: it orients straight
  wires toward +X, +Y, or +Z (adding 180° where NEC's end-1-to-end-2 direction runs
  the other way), references phase to the first feed current, and computes the
  instantaneous current over the RF cycle. `displayedCurrentsAt` picks one frequency's
  currents; the Currents view (Antenna tab and table) and the geometry views' Currents
  colouring all consume it. The geometry views take the results column's state through
  `ResultsController::setColumnChangedCallback`, so they follow the selected frequency and
  live points, and draw the wires plain while that frequency is stale.
- `ui/ColorScale` holds the one magnitude scale (Turbo) and its legend, used for radiation
  gain and for current on the wires in the 3D pattern and the geometry views.
- Radiation datasets retain their frequency and request identity.
- `analysis/GainDirection` turns NEC θ/φ into elevation/azimuth and formats every
  displayed gain, cut name, reference level, and F/B or F/R definition, so widgets
  never compose that text themselves. `analysis/PatternRequests` builds the RP
  presets (5° grid, azimuth cut at an elevation, elevation cut through an azimuth,
  cuts through a peak). The Requests editor, the optimizer's rear-hemisphere grid, and
  Find Sampled Peak all use them.
- Historical views use the archived generated deck for the matching antenna overlay.

Directional metrics share one non-widget implementation. Forward gain uses an
explicit spherical direction. Every F/B uses one back direction, the same elevation
at the opposite azimuth, which specified-direction requests sample explicitly and
tracked cuts contain; missing coverage produces an unavailable value rather than a
substitute. F/R compares the forward sample with the strongest sampled response in the
rear half of the requested cut. [Directional Metrics](optimization-math.md#directional-metrics)
defines each case.

## Optimization

All optimization methods use a shared candidate evaluator. It:

1. Applies selected `SY` values.
2. Resolves and validates a numeric deck.
3. Generates only the requests needed by the objective.
4. Runs the configured external solver.
5. Parses candidate results.
6. Produces one lower-is-better objective score and its criterion breakdown.

Parameter Sweep, Adaptive Optimize, Nelder–Mead, and Differential Evolution differ
only in how they propose bounded candidates. They share variables, frequency plans,
objectives, artifacts, cancellation, and result presentation.

Each objective criterion independently defines a goal, target or threshold when
needed, weight, and reduction across frequencies. Candidate results retain the raw
per-frequency measurements, reduced values, extrema, and weighted contributions so
the ranking remains inspectable rather than a black box.

Broad model radiation requests are omitted from ordinary impedance-only candidates.
Directional objectives generate focused samples at each study frequency, reducing
solver work while preserving the stated metric definition.

An optimization baseline is a durable analysis of the actual current model, not an
optimizer candidate. Baseline identity and coverage are modeled independently from
the results column's optional historical overlay so those two comparisons cannot
silently replace one another. When the current study lacks a compatible baseline,
the optimization workspace requests one through the ordinary MainWindow analysis
pipeline with an optimization-specific generated deck containing only the study
frequencies and objective-required outputs. It then begins candidate evaluation only
after the run is parsed and indexed. This internal run is archived but does not
replace the active live-result state.
Directional baseline compatibility compares the exact generated theta/phi request
grid, not only the presence of radiation data at a frequency. This keeps candidate
overlays and directional objective reductions on the same sampled cut.
The optimization session stores the baseline run ID; historical session review loads
that archived result without replacing the active model's live-result state.

Candidate comparison is owned by a focused panel inside the reusable candidate-review
window, leaving the main evaluation table and search-progress plot
uncompressed. It reuses the normal SWR/impedance and 2D-pattern views, placing the
compatible baseline behind the selected candidate as a labeled dashed series. The
core fingerprint and coverage evaluator is Qt-free; widgets only present its status
and data. Optimization candidates are not injected into the results column;
their historical-overlay controller remains exclusively run-to-run comparison.

The optimization workspace resolves and fingerprints the active deck once per model
change, and keeps the latest pattern's peak and tracked-cut metrics until a new
pattern arrives, so frequent readiness refreshes and objective-preset changes do not
re-parse source or rescan large patterns.

The optimizer lives in `OptimizerWindow`, a top-level window with no parent, so it and
the main window come to the front independently; MainWindow owns it, closes it when it
closes, and shares Save, Undo and Redo with it. Closing it only hides it, keeping the
`OptimizationWorkspace` and any running study. It saves its geometry under
`optimizerWindow/geometry`; with none saved, or none on a connected screen, it opens at
about 80% of the main window on another screen, else beside the main window, else over
its right-hand side. In a narrow window its setup stacks above its results in a vertical
scroll area. A study evaluates the source captured when it started; edits of
the same model keep its candidates, and a new or opened model clears them. Applying a
candidate to a model that changed since the study started first asks, listing the
differences from `nec::describeSourceChanges` (variables, and card fields paired by an
alignment of the card sequence), which ignores the optimized variables' values.

Applying a candidate is an explicit source edit to active `SY` definitions. It is
undoable and does not mutate archived candidate artifacts. Applying and running
then uses the normal analysis pipeline to create an ordinary result.

## Persistence and Presentation

`QSettings` stores window state, display preferences, the chosen engine and the
executable paths the user picked, using the platform's normal user settings location. Run artifacts remain in
the application's local data directory.

Common numeric presentation is centralized in `ui/DisplayFormat.h`. The card tables
and Wires use `CompactNumberDelegate` to show long numbers at 6 significant figures
while editing starts from the exact source text. Display rounding never changes
source text, archived solver output, exports, or internal calculation precision.

See the [Roadmap](roadmap.md) for planned work and the
[NEC Card Support](nec-card-support.md) matrix for card-level behavior.
