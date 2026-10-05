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

The top-level workspaces are Home, Model, Analysis, Results, and Optimize.

- **Home** summarizes the active model, recent results, and model quality.
- **Model** owns geometry, parameters, sources, loads/networks, environment, and NEC source.
- **Analysis** owns frequency, solver configuration, and output requests.
- **Results** owns current or historical parsed output.
- **Optimize** owns parameter studies and candidate review.

The compact workspace navigator handles destinations; menus expose the complete
command set; the toolbar contains frequent global actions. Project visibility is
reduced automatically in space-intensive workspaces.

Results and candidate details use reusable detachable windows. A detached view
is the same live content, not a copied result model. Historical Run Review is
separate and read-only so opening archived output cannot silently replace the
active editor.

## Validation

Model checking runs after source changes and immediately before a solve. Blocking
syntax, field, ordering, expression, or reference errors prevent analysis and
optimization. Adequacy findings are warnings because their importance depends on
the model and engineering purpose.

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
- 4nec2 NEC2dXS receives filenames through standard input.

Each backend has an independently persisted executable path. All adapters feed
the same run store and backend-neutral result model. No solver is bundled.

Solver execution is asynchronous and supports live output, cancellation, and a
timeout. Every run receives a unique durable directory containing source,
generated input, output, logs, and versioned metadata. Startup discovery rebuilds
Run History from those directories and marks interrupted records appropriately.

## Requests and Frequency Plans

The authored model may contain an ordinary `FR` sweep and one or more result
requests. Workbench can either retain that native sequence or generate explicit
single-frequency request groups for selected frequencies or custom ranges.

This permits impedance across a broad sweep while calculating expensive radiation
patterns only where requested. Generated request policy is recorded with the run;
it does not rewrite the authored cards.

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
