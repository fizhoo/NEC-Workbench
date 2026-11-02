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
unknown extensions. Raw Source and Structured Cards are two views of this same
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

Human-readable operation metadata supplements Qt's text undo stack. Undo and Redo
may navigate back to the workspace that originated an edit, but never maintain a
second model state.

Symbolic geometry is protected from operations that would replace expressions
with resolved numbers. Field parameterization occurs through Structured Cards;
Model Parameters edits the resulting `SY` definitions. Optimize consumes those
same definitions.

## Workspaces

Home is the main window: a compact control panel (`ui/controlpanel`) whose layout is
fixed to its content. It summarizes the active model, selected-frequency quick results,
model quality, and the latest run, and launches the focused windows without
duplicating their content. Every other workspace is a separate top-level window:

- **Geometry Editor** owns the 2D and 3D geometry views.
- **NEC Deck** owns Raw Source, Structured Cards, and Parameters.
- **Setup** owns frequency, sources, loads/networks, environment, output requests,
  and solver configuration.
- **Impedance / SWR**, **Currents**, **Radiation**, **Raw Solver Output**, and
  **Run History** present current or historical parsed output.
- **Optimize** owns parameter studies and candidate review.

Work windows are `DetachablePanel`s, and result windows are coordinated by
`ResultWindowCoordinator`. Each window persists its own geometry, so resizing one
never resizes another, and opening a window again raises the existing instance.
Menus expose the complete command set; a compact navigator selects pages inside
Setup and the NEC Deck window.

A detached view is the same live content, not a copied result model. Historical Run
Review is separate and read-only so opening archived output cannot silently replace
the active editor. The historical-overlay controller selects at most one archived run
and never changes the active result; each result window independently decides whether
to display it.

## Validation

Model checking runs after source changes and immediately before a solve. Blocking
syntax, field, ordering, expression, or reference errors prevent analysis and
optimization. Adequacy findings are warnings because their importance depends on
the model and engineering purpose.

Validation commands are grouped in the **Validate** menu. Static findings remain
in a reusable non-modal report, while the focused **Validation** window owns the
compact overview, Average Gain Test result, and segmentation-convergence workspace.
The Run Monitor remains execution UI rather than a validation result; durable runs
are reviewed through Run History and the read-only Run Review window.

The static checker covers geometry/control-card ordering, segment length,
thin-wire ratios, source placement, adjoining segmentation, and large junctions.
Average Gain Test and segmentation convergence are solver-backed studies with
their own archived artifacts; they are not inferred from static checks.

Automatic Segmentation is an explicit preview-and-apply operation. It changes
`GW` segment counts and remaps supported `EX`, `LD`, and `TL` attachments by
relative wire position in one undoable edit. Unsupported attachment forms block
the operation rather than being guessed.

## Symbols and Solver Decks

Workbench `SY` expressions are solver-independent. The core resolver supports
earlier symbol references, parentheses, and arithmetic operators. A run archives:

- `model.source.nec` — the authored source, including symbols
- `model.nec` — the generated numeric deck sent to the solver

Workbench-only compatibility cards are normalized or removed at this boundary
when required by a strict NEC-2 backend. The active source is never replaced by
the generated deck.

Quick sweeps and custom pattern-frequency plans also operate at this boundary.
They alter only the generated deck and retain the authored model unchanged.

## Solver Integration

The selected backend identifies a process protocol, not merely an executable name:

- `nec2c` uses attached input/output arguments.
- OpenNEC uses positional input with original-format output options.
- 4nec2 NEC2dXS receives filenames through standard input. Its generated numeric deck
  uses canonical space-delimited fields so direct execution never depends on the full
  4nec2 application's input preprocessing or on unsupported tab separators.

Each backend has an independently persisted executable path. All adapters feed
the same run store and backend-neutral result model. No solver is bundled.

Solver execution is asynchronous and supports live output, cancellation, and a
timeout. Every run receives a unique durable directory containing source,
generated input, output, logs, and versioned metadata. Startup discovery rebuilds
Run History from those directories and marks interrupted records appropriately.

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
  instantaneous current over the RF cycle. The Currents window's Antenna tab and the
  table's relative-phase column both consume this core result.
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
the live result windows' optional historical overlay so those two comparisons cannot
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

Candidate comparison is owned by a focused panel inside the reusable detached
candidate-preview window, leaving the main evaluation table and search-progress plot
uncompressed. It reuses the normal SWR/impedance and 2D-pattern views, placing the
compatible baseline behind the selected candidate as a labeled dashed series. The
core fingerprint and coverage evaluator is Qt-free; widgets only present its status
and data. Optimization candidates are not injected into the live result windows;
their historical-overlay controller remains exclusively run-to-run comparison.

The optimization workspace resolves and fingerprints the active deck once per model
change, and keeps the latest pattern's peak and tracked-cut metrics until a new
pattern arrives, so frequent readiness refreshes and objective-preset changes do not
re-parse source or rescan large patterns.

Applying a candidate is an explicit source edit to active `SY` definitions. It is
undoable and does not mutate archived candidate artifacts. Applying and running
then uses the normal analysis pipeline to create an ordinary result.

## Persistence and Presentation

`QSettings` stores window state, display preferences, and per-backend executable
paths using the platform's normal user settings location. Run artifacts remain in
the application's local data directory.

Common numeric presentation is centralized in `ui/DisplayFormat.h`. Structured Cards
and Wires use `CompactNumberDelegate` to show long numbers at 6 significant figures
while editing starts from the exact source text. Display rounding never changes
source text, archived solver output, exports, or internal calculation precision.

See the [Roadmap](roadmap.md) for planned work and the
[NEC Card Support](nec-card-support.md) matrix for card-level behavior.
