# Optimization Mathematics

This guide describes the mathematics actually implemented by NEC Workbench. It
is intended to make optimization studies understandable and reproducible without
turning the user guide into a numerical-analysis textbook.

All search methods use the same candidate evaluator. The selected parameter
values generate a numeric NEC deck, the configured solver produces impedance and
optional radiation results, and the objective builder converts those results into
one scalar score. Every optimizer minimizes that score; the optimizer algorithm
does not change the meaning of the objective.

## Notation

- `x` is a candidate parameter vector.
- `x_i` is parameter `i`.
- `f_j` is study frequency `j`.
- `m_k(x, f_j)` is the measured value for criterion `k` at that candidate and
  frequency.
- `w_k` is the nonnegative weight for criterion `k`.
- `t_k` is a Target or Good Enough value.
- `s_k` is the criterion scale used to keep unlike units comparable.
- `J(x)` is the final lower-is-better objective score.

The current criterion scales are:

| Criterion | Scale `s_k` |
|---|---:|
| SWR | 1 |
| Resistance | Reference impedance `Z0` |
| Reactance | Reference impedance `Z0` |
| Forward gain | 10 dB |
| F/B | 10 dB |
| F/R | 10 dB |

These scales are normalization choices, not physical conversions.

## Per-Frequency Criterion Cost

For a measured value `m`, the selected Goal creates a lower-is-better cost:

### Minimize

```text
c(m) = m / s
```

Lower measurements produce lower scores.

### Maximize

```text
c(m) = -m / s
```

The minus sign converts maximization into minimization. A negative contribution
is valid; only relative scores matter.

### Target

```text
c(m) = |m - t| / s
```

Values on either side of the target are penalized by their absolute distance.

### Good Enough ≤

```text
c(m) = max(0, m - t) / s
```

There is no penalty at or below the threshold.

### Good Enough ≥

```text
c(m) = max(0, t - m) / s
```

There is no penalty at or above the threshold.

For example, with **SWR · Good Enough ≤ 2.0**, SWR values `1.5`, `2.0`, and
`2.7` produce costs `0`, `0`, and `0.7` before weighting.

## Reduction Across Frequencies

Each criterion independently selects a reducer `R` over the frequency plan:

```text
R_min(y_1 ... y_N) = min(y_j)
R_avg(y_1 ... y_N) = (1/N) Σ y_j
R_max(y_1 ... y_N) = max(y_j)
```

More explicitly, the reduced criterion cost is:

```text
Minimize:      C_k(x) =  R(m_k(x, f_j)) / s_k
Maximize:      C_k(x) = -R(m_k(x, f_j)) / s_k
Target:        C_k(x) =  R(|m_k(x, f_j) - t_k|) / s_k
Good Enough ≤: C_k(x) =  R(max(0, m_k(x, f_j) - t_k)) / s_k
Good Enough ≥: C_k(x) =  R(max(0, t_k - m_k(x, f_j))) / s_k
```

Thus **Maximize Minimum** first finds the lowest measured gain across the band and
then negates that value for the lower-is-better score. **Target Maximum** finds the
largest absolute target error.

The UI presents these reducers as goal-aware **Band Evaluation** choices:
**Worst Point**, **Average**, and **Best Point**. Target and Good Enough rows use
**Error** or **Violation** in those labels. This avoids combinations such as
“Minimize / Maximum” while preserving the exact reducer shown in the equations.
The selected extremum retains its frequency. An arithmetic average describes the
whole selected frequency set and therefore intentionally has no single frequency.
Candidate inspection also preserves every raw per-frequency metric so its table,
directional plots, and reported extrema can be audited against solver output.

Useful robust defaults are:

- SWR: **Minimize Maximum**
- Resistance: **Target 50 Ω, Maximum error**
- Reactance: **Target 0 Ω, Maximum error**
- Gain, F/B, and F/R: **Maximize Minimum**

Selecting Average rewards overall performance. Selecting the worst-direction
reducer—Maximum error for minimized/target criteria or Minimum measurement for
maximized criteria—prevents a weak frequency from being hidden by stronger ones.

## Weights and Total Score

Let `C_k(x)` be the reduced cost for criterion `k`. The total score is:

```text
                 Σ w_k C_k(x)
J(x) =          ----------------
                    Σ w_k
```

Only criteria with positive weights participate. Weight normalization means:

- Multiplying every enabled weight by the same number does not change ranking.
- If SWR is the only enabled criterion, changing its weight has no effect.
- If SWR has weight 3 and gain has weight 1, SWR contributes three times as
  strongly as gain after their unit scales are applied.
- A satisfied Good Enough criterion contributes zero and leaves the remaining
  criteria to distinguish candidates.

## Parameter Sweep

For one selected variable with bounds `a` and `b` and `P` sweep points, candidate
`p` is:

```text
x_p = a + p (b - a) / (P - 1),    p = 0 ... P - 1
```

Both endpoints are included. Every candidate is evaluated independently at the
same frequency plan. Parameter Sweep performs no convergence inference; it simply
returns the best tested point and the complete sampled landscape.

## Adaptive Optimize

Adaptive Optimize is a bounded coordinate-refinement search. It is intentionally
simple and inspectable.

For `n` parameters with bounds `[a_i, b_i]`, the initial center is:

```text
x_i^(0) = (a_i + b_i) / 2
```

The initial set contains the center plus the minimum and maximum of each variable
while all other variables remain centered. The maximum initial size is therefore:

```text
1 + 2n
```

The initial coordinate step is one quarter of each parameter range:

```text
Δ_i^(0) = (b_i - a_i) / 4
```

After a complete evaluated batch, let `x*` be the successful candidate with the
lowest score. The next batch tests, for each eligible coordinate:

```text
x_trial = clamp(x* ± Δ_i e_i, a, b)
```

where `e_i` changes only coordinate `i`. Duplicate points and moves smaller than
that parameter's tolerance are omitted. After proposing a refinement batch, every
step is halved:

```text
Δ_i <- Δ_i / 2
```

The score improvement reported for a completed round is:

```text
improvement_r = best_score_(r-1) - best_score_r
```

Adaptive Optimize stops when any of these occurs:

1. The maximum evaluation count is reached.
2. No successful candidate exists.
3. No nonduplicate coordinate move remains above parameter tolerance.
4. Two consecutive completed rounds improve the best score by no more than the
   score tolerance.

Parameter tolerance controls geometric search resolution. Score tolerance controls
objective improvement; they are different units and should not be compared.

## Nelder–Mead

Nelder–Mead maintains a simplex of `n + 1` parameter vectors. Workbench starts at
the current clamped parameter vector and creates one additional vertex per variable
using 20% of that variable's permitted range.

At each iteration, sort the simplex so:

```text
J(x_1) ≤ J(x_2) ≤ ... ≤ J(x_(n+1))
```

`x_1` is best and `x_(n+1)` is worst. The centroid excluding the worst vertex is:

```text
c = (1/n) Σ_(i=1 to n) x_i
```

Workbench uses the standard coefficients:

```text
reflection α = 1
expansion  γ = 2
contraction ρ = 0.5
shrink      σ = 0.5
```

### Reflection

```text
x_r = c + α(c - x_(n+1))
```

If the reflected point is better than the best point, Workbench tries expansion.
If it is better than the second-worst point, reflection replaces the worst point.

### Expansion

```text
x_e = c + γ(x_r - c)
```

The better of `x_e` and `x_r` replaces the worst vertex.

### Contraction

Outside contraction uses the reflected point when reflection is better than the
worst point:

```text
x_c = c + ρ(x_r - c)
```

Inside contraction uses the old worst point otherwise:

```text
x_c = c + ρ(x_(n+1) - c)
```

If contraction is not accepted, the simplex shrinks.

### Shrink

Every non-best vertex moves halfway toward the best:

```text
x_i <- x_1 + σ(x_i - x_1),    i = 2 ... n + 1
```

Every proposed coordinate is clamped to its configured bounds. Nelder–Mead stops
when the evaluation budget is exhausted, no successful simplex exists, all simplex
coordinates are within their parameter tolerances of the best vertex, or score
spread remains within score tolerance for two consecutive checks and the current
simplex has contracted to at most the larger of parameter tolerance and 1% of
parameter range.

## Differential Evolution

NEC Workbench implements bounded, seeded **DE/rand/1/bin**. Differential Evolution
is a population method and is often better than a local method when the score
surface contains separated valleys.

The initial population contains the current clamped model plus `NP - 1` uniformly
random vectors within the configured bounds. A fixed random seed reproduces the
same initial vectors and trial sequence.

For target member `x_i`, choose three distinct population members `x_a`, `x_b`,
and `x_c`, none equal to the target. Mutation creates:

```text
v = x_a + F(x_b - x_c)
```

`F` is the Mutation factor. Binomial crossover then creates trial vector `u`:

```text
u_j = v_j    if rand(0,1) ≤ CR or j = j_forced
u_j = x_i,j  otherwise
```

`CR` is the Crossover rate. One randomly selected forced coordinate guarantees
that every trial inherits at least one mutant value. The trial is clamped to the
parameter bounds.

Selection is greedy:

```text
u replaces x_i if J(u) ≤ J(x_i)
```

After every population member receives one trial, a generation is complete.
Differential Evolution stops when:

1. The configured generation limit is reached.
2. Every parameter's population spread is within that parameter's tolerance.
3. The best-score improvement is no greater than score tolerance for three
   consecutive generations.
4. The initial population contains no successful candidate.

With population `NP` and `G` evolved generations, the maximum planned candidate
count is:

```text
NP (G + 1)
```

The extra population is the initial generation.

## Failed Candidates and Bounds

A candidate whose solver run or required result extraction fails has no finite
score. Adaptive Optimize ignores it when choosing the best point. Nelder–Mead and
Differential Evolution treat it as positive infinity, so it cannot replace a
successful candidate.

All optimizer proposals are clamped to user bounds. A best candidate on a bound
is not mathematically invalid, but it is evidence that a better point may exist
outside the requested search region.

## Choosing a Method

- Use **Parameter Sweep** to understand one variable and verify the score landscape.
- Use **Adaptive Optimize** for transparent, bounded coarse-to-fine refinement.
- Use **Nelder–Mead** for a small number of interacting continuous variables when
  a local search is appropriate.
- Use **Differential Evolution** when multiple local optima are plausible or when
  broader global exploration is worth additional solver evaluations.

The method changes how candidates are proposed, not how NEC results are scored.
