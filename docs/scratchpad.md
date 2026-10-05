# NEC Workbench Scratchpad

Unscheduled ideas, not release commitments. Implemented work belongs in the user
guide and architecture rather than this list.

## Results and RF Tools

- Smith chart using NEC impedance sweeps, followed by interactive markers and reference-impedance controls
- Shared transmission-line core for transformation, loss, electrical length, SWR,
  open/shorted stubs, matching, and phasing lines
- Explicit transfer between NEC feedpoint results and standalone RF calculators
- Display normalization from solved excitation to a selected accepted input power,
  with peak/RMS labeling and original values retained
- CSV export for impedance, SWR, current, gain, and directional tables
- Deliberate two-run overlays, starting with impedance and SWR

## Visualization

- Optional filled or smoothly shaded 3D radiation surfaces with a numeric color scale
- Additional polarization and field-component views
- Near-field parsing and visualization

## Analysis Requests

- Expand amateur-band pattern presets beyond center frequency to optional band
  edges or full-band sweeps, with the resulting calculation count shown first

## Optimization

- Finalist sensitivity and construction-tolerance analysis
- Per-frequency or per-band importance
- Explicit pass/fail constraints in addition to weighted preferences
- Two-variable grid sweeps and objective heat maps
- Baseline-versus-finalist comparison
- Staged impedance filtering before expensive directional evaluation
- Revisit whether **Apply This Candidate and Run** adds enough value beyond Apply
  and the normal Run command

## Modeling and Delivery

- Export a strict numeric NEC deck without Workbench extensions
- Guided antenna templates and creator
- Signed installers and longer-lived release packages
- Plugin boundaries only after model, solver, result, and optimization APIs stabilize
