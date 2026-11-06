# Clustered lighting benchmark

## Method

These are earlier render-thread CPU measurements. Each value is a single-frame measurement, not an averaged frame time or a presentation-timing result. Later frame-capped averages are discarded. No further presentation-timing investigation is included.

| Scene | Fallback | Clustered | Cluster assignment |
| --- | ---: | ---: | ---: |
| Sparse, 3 visible lights | 1.08492 ms | 1.16667 ms | 0.076625 ms |
| Dense, 133 visible lights | 3.13054 ms | 1.17783 ms | 0.092459 ms |

The assignment column is the grid clear and light-assignment setup cost measured alongside the clustered frame.

## Results

- Dense: clustered rendering is about 2.66x faster than fallback. Draw count drops from 218 to 86.
- Sparse: clustered rendering is about 7.6% slower than fallback because the fixed grid setup cost is not amortized across many local lights.
- Overflow stress: the 134-light case saturates at the per-cluster maximum of 128 lights and reports 7,370 dropped assignments. The excess assignments are dropped deterministically and fallback switching remains valid.

These draw counts are submission statistics; no separate GPU timestamp or presentation-rate conclusion is claimed.
