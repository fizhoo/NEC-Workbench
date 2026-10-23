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

The top-level destinations are Home, Model, Setup, Results, and Optimize. The UI is
intentionally hybrid rather than a single deeply nested tab stack: Home remains the
operations base, while focused editors and result views can stay open beside it.

- **Home** summarizes the active model, recent results, model quality, and recent runs,
  and launches the focused workspaces without duplicating their full content.
- **Model** owns geometry, parameters, and NEC source.
- **Setup** owns frequency, sources, loads/networks, environment, solver configuration,
  and output requests.
- **Results** owns current or historical parsed output.
- **Optimize** owns parameter studies and candidate review.

The compact workspace navigator handles pages inside Model, Setup, and Results;
menus expose the complete command set; the toolbar contains frequent global actions.
Model and Setup use reusable detachable panels. Impedance, Currents, Radiation, and
Run History use focused reusable windows so they can remain visible while the model
is edited and rerun.

Results and candidate details use reusable windows. A detached view is the same live
content, not a copied result model. Historical Run Review is separate and read-only
so opening archived output cannot silently replace the active editor. The Results
historical-overlay controller selects at most one archived run and never changes the
active result; each result window independently decides whether to display it.

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
- Radiation datasets retain their frequency and request identity.
- Historical views use the archived generated deck for the matching antenna overlay.

Directional metrics share one non-widget implementation. Forward gain uses an
explicit spherical direction. F/B requires the physical antipodal direction;
missing coverage produces an unavailable value. F/R compares the forward sample
with the strongest sampled response in the documented rear azimuth cut.

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

Applying a candidate is an explicit source edit to active `SY` definitions. It is
undoable and does not mutate archived candidate artifacts. Applying and running
then uses the normal analysis pipeline to create an ordinary result.

## Persistence and Presentation

`QSettings` stores window state, display preferences, and per-backend executable
paths using the platform's normal user settings location. Run artifacts remain in
the application's local data directory.

Common numeric presentation is centralized in `ui/DisplayFormat.h`. Display
rounding never changes source text, archived solver output, exports, or internal
calculation precision.

See the [Roadmap](roadmap.md) for planned work and the
[NEC Card Support](nec-card-support.md) matrix for card-level behavior.
