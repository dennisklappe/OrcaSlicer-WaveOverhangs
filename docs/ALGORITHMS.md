# Wave Overhang Algorithm

This document explains how the wave-overhang generator builds toolpaths, how it iterates step by step, and where to find the source that implements it.

## Contents

1. [Overview](#overview)
2. [How it iterates](#how-it-iterates)
3. [Key parameters](#key-parameters)
4. [Source](#source)

---

## Overview

The generator computes a **seed** geometry at or near the supported edge of the overhang, then propagates wavefronts outward from that seed into the unsupported region. Each new front bonds to the previous one. The loop ends when fronts can no longer grow further inside the current layer.

The seed is a narrow band along the support-overhang boundary. Each iteration offsets the accumulated covered region outward by the configured line spacing and emits a polyline along the new front. A pattern mode (Smart / Monotonic / ZigZag) controls how the resulting fronts are traversed.

![Wavefront propagation](images/algorithms/propagation.svg)

## How it iterates

```mermaid
flowchart TD
    A[Entry point:<br/>overhang_area + lower_slices + config]
    B[Prep seed:<br/>overhang at neighbourhood of lower-slice boundary<br/>expand by seed_expansion]
    C[Prep wave_cover_polygons:<br/>the region fronts are allowed to live in]
    D[Prep trim_boundary:<br/>wave_cover shrunk by line_width/2<br/>keeps extrusion off the outer wall]
    E[current_shape = intersection of<br/>offset seed by seed_expansion<br/>with wave_cover_polygons]
    F[next_shape = intersection of<br/>offset current_shape by wave_spacing<br/>with wave_cover_polygons]
    G{next_shape empty?}
    H[Done:<br/>emit accumulated fronts<br/>per selected pattern mode]
    I[next_area = area of next_shape]
    J{new_area > min_area_growth?}
    K[Clip next_shape outline to trim_boundary<br/>simplify + reconnect polyline fragments]
    L[Push fragments into front_levels stack]
    M[current_shape = next_shape]
    N[Pattern mode:<br/>Smart / Monotonic / ZigZag<br/>decides inter-front traversal]
    O[Emit as ExtrusionPath<br/>mm3/mm = wave_overhang_flow_mm3_per_mm<br/>defaults to nozzle squared]

    A --> B --> C --> D --> E --> F --> G
    G -- yes --> H
    G -- no --> I --> J
    J -- no --> H
    J -- yes --> K --> L --> M --> F
    H --> N --> O
```

## Key parameters

- `wave_overhang_line_spacing`: distance between successive wavefronts
- `wave_overhang_pattern`: Smart / Monotonic / ZigZag, affects how fronts are traversed after all are generated
- `wave_overhang_perimeter_overlap`: how far waves extend toward the outer wall
- `wave_overhang_minimum_width`: split waves at narrow necks narrower than this
- `wave_overhang_min_new_area`: saturation threshold
- `wave_overhang_flow_mm3_per_mm`: per-millimetre extruded volume; see [shared flow setting](#flow-setting)

## Flow setting

`wave_overhang_flow_mm3_per_mm` controls how much plastic is extruded per millimetre of wave-overhang line. The default is `0.16` mm³/mm, which equals `nozzle²` for a 0.4 mm nozzle.

Why a fixed mm³/mm rather than a layer-height-dependent ratio: a wave-overhang line hangs in air, not squished against a layer below. There's nothing to squish into, so layer height has no effect on the bead's cross-section. Only the nozzle bore and the mm³/mm extrusion rate set the bead size.

Recommended values for other nozzle sizes:

| Nozzle | `wave_overhang_flow_mm3_per_mm` |
|---|---|
| 0.3 mm | 0.09 |
| 0.4 mm | **0.16 (default)** |
| 0.5 mm | 0.25 |
| 0.6 mm | 0.36 |
| 0.8 mm | 0.64 |

Raise if wave lines look thin or broken; lower if they blob together.

## Source

- `src/libslic3r/WaveOverhangs/WaveOverhangs.cpp`: the algorithm body
- `src/libslic3r/WaveOverhangs/AndersonsGenerator.cpp`: pluggable wrapper
