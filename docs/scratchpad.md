# NEC Workbench Scratchpad

Ideas recorded here are discussion items, not implementation commitments. Status values may include Parking Lot, Near-Term, Planned, Implemented, Rejected, or Superseded.

## Plotting and Transmission Lines

### Smith Chart and Transmission-Line Tools

- **Status:** Parking Lot
- Add a Smith chart for impedance, admittance, SWR circles, and frequency traces.
- Consider transmission-line calculators for line transformation, loss, electrical length, and SWR.
- Integrate these tools with NEC results so a calculated feedpoint impedance can become the chart or calculator input without manual re-entry.
- Keep standalone calculator use available even when no NEC model is open.

**Direction:** Build this in layers: first a read-only Smith chart for NEC frequency results, then interactive markers and reference-impedance controls, followed by standalone transmission-line calculations. This avoids tying the initial chart implementation to a large calculator subsystem.

## 3D Visualization

### Radiation Color Contours and Shading

- **Status:** Parking Lot
- Add gain-based color contours or smooth color shading to 3D radiation patterns.
- Provide a visible dBi color scale so color has an unambiguous numerical meaning.
- Consider selectable absolute-gain and normalized-to-peak coloring.
- Preserve antenna geometry visibility and readable surface shape when coloring is enabled.

**Direction:** Start with a simple continuous color gradient mapped to gain and a legend. Add optional contour bands only if they improve interpretation; avoid adding multiple overlapping display modes initially.

### Input-Power Normalization

- **Status:** Parking Lot / Likely Near-Term
- Allow results produced with a conventional `EX` voltage source, often `1 + j0` V, to be displayed at a user-selected accepted input power such as 100 W.
- Scale current, source voltage, and field amplitudes by `sqrt(requested_power / solved_input_power)`; impedance, SWR, gain, and pattern shape remain unchanged in a linear model.
- Offer **As Solved** and **Normalize to Input Power** display modes without silently rewriting the authored `EX` card.
- Clearly label whether displayed amplitudes are NEC peak phasors or RMS values; 4nec2 presents voltage and current as RMS values.
- Show normalized segment currents in amperes and source voltage/current where available.
- Do not initially claim a general voltage distribution along every antenna wire: standard NEC output directly provides segment currents and driving-point voltage/current, but not a simple circuit-like voltage value at every wire segment.

**Direction:** Implement normalization as a results-display transformation using the solver's reported accepted input power. Keep the original solver values available for reproducibility and apply the same scale consistently to currents and field strengths.

## Radiation Requests

### Amateur-Band Pattern Frequencies

- **Status:** Center Implemented; Expanded Modes Parking Lot
- Extend **Amateur Band Centers…** with selectable frequency coverage modes.
- **Center** already adds one representative center frequency for each selected band.
- **Band Edges** adds the lower and upper engineering edges for each selected band.
- **Full Band Sweep** adds the complete preset range and step for each selected band.
- Show the resulting frequency and pattern-calculation counts before accepting the selection.
- Keep generated frequencies editable and remove duplicates automatically.

**Direction:** Implement center frequencies first because radiation calculations are comparatively expensive. Add edge and full-sweep modes only after the simpler picker is tested and the workload impact remains obvious.

## Optimization

### Candidate Result Exploration

- **Status:** Implemented Experiment / Revisit; Potential Revert
- Optimization candidates currently belong to an optimization session rather than the normal analysis Results workflow.
- Preserve candidate SWR, impedance, gain, and other available result curves so they remain inspectable after optimization completes.
- Keep these results clearly identified as optimization-candidate data rather than ordinary model runs.
- Allow a selected candidate to open detailed plots without replacing the active model or pretending the candidate is the active model's official result.
- Consider an explicit **Promote Candidate** or **Apply and Run** action when the user wants a candidate to become a normal model analysis.

**Direction:** Treat the optimization session as its own result container. The optimizer should retain summary curves for all candidates and permit detailed inspection of one candidate at a time. A candidate only becomes a normal Results run after an explicit action, keeping run history understandable while preserving useful optimization data.

**Revisit note:** Candidate inspection plus **Apply This Candidate to Model** and
**Apply This Candidate and Run** are intentionally isolated in the reusable details
window. Re-evaluate whether the run shortcut adds enough value beyond **Apply Best
to Model** and the normal Run action. Keep this checkpoint easy to remove if it
makes the optimization workflow feel ambiguous or crowded. For the current trial,
applying a candidate intentionally preserves its frequency table and SWR/R/X plots;
unrelated subsequent model edits still clear stale study data.

## Results Comparison

### Multiple Run Graph Overlays

- **Status:** Parking Lot / Likely Near-Term
- Overlay impedance, SWR, gain, and other compatible result curves from multiple runs.
- Keep every curve clearly labeled by source model, run date, and candidate or analysis identity.
- Allow individual runs and quantities to be shown or hidden without reopening them.
- Require compatible result types and coordinate/frequency domains; explain when two runs cannot be overlaid directly.
- Keep comparison selections separate from the active model and ordinary single-run Results context.

**Direction:** First add a deliberate **Compare Runs** workflow for two ordinary
analysis runs, beginning with impedance and SWR. Add gain and radiation-pattern
overlays only after frequency, cut-plane, polarization, and normalization matching
are explicit enough to prevent misleading comparisons.
