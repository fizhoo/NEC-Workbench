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

## Optimization

### Candidate Result Exploration

- **Status:** Parking Lot / Likely Near-Term
- Optimization candidates currently belong to an optimization session rather than the normal analysis Results workflow.
- Preserve candidate SWR, impedance, gain, and other available result curves so they remain inspectable after optimization completes.
- Keep these results clearly identified as optimization-candidate data rather than ordinary model runs.
- Allow a selected candidate to open detailed plots without replacing the active model or pretending the candidate is the active model's official result.
- Consider an explicit **Promote Candidate** or **Apply and Run** action when the user wants a candidate to become a normal model analysis.

**Direction:** Treat the optimization session as its own result container. The optimizer should retain summary curves for all candidates and permit detailed inspection of one candidate at a time. A candidate only becomes a normal Results run after an explicit action, keeping run history understandable while preserving useful optimization data.
