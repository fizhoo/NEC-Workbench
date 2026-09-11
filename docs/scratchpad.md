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

## Optimization

### Candidate Result Exploration

- **Status:** Parking Lot / Likely Near-Term
- Optimization candidates currently belong to an optimization session rather than the normal analysis Results workflow.
- Preserve candidate SWR, impedance, gain, and other available result curves so they remain inspectable after optimization completes.
- Keep these results clearly identified as optimization-candidate data rather than ordinary model runs.
- Allow a selected candidate to open detailed plots without replacing the active model or pretending the candidate is the active model's official result.
- Consider an explicit **Promote Candidate** or **Apply and Run** action when the user wants a candidate to become a normal model analysis.

**Direction:** Treat the optimization session as its own result container. The optimizer should retain summary curves for all candidates and permit detailed inspection of one candidate at a time. A candidate only becomes a normal Results run after an explicit action, keeping run history understandable while preserving useful optimization data.
