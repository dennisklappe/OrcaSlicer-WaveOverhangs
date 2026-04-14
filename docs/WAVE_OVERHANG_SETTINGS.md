# Wave Overhang Settings Reference

This document describes every config option added by the wave-overhangs fork of OrcaSlicer, what it does, and how to tune it. For an overview of what wave overhangs are and why they exist, see the [main README](../README.md).

## Contents

1. [What wave overhangs are](#what-wave-overhangs-are)
2. [How to enable](#how-to-enable)
3. [Tier 1 — Simple mode](#tier-1--simple-mode)
4. [Tier 2 — Advanced tuning](#tier-2--advanced-tuning)
   - [General](#general)
   - [Geometry](#geometry)
   - [Anchoring](#anchoring)
   - [Anderson-specific](#anderson-specific)
   - [Kaiser LaSO](#kaiser-laso)
   - [Speed](#speed)
   - [Cooling](#cooling)
   - [Floor layers](#floor-layers)
   - [Support integration](#support-integration)
   - [Debug](#debug)
5. [Recipe bundles](#recipe-bundles)
6. [Known limitations](#known-limitations)
7. [G-code markers](#g-code-markers)

---

## What wave overhangs are

Wave overhangs let you print steep cantilevered overhangs without supports. Instead of dropping support columns from the bed, each ring of extrusion anchors to the one before it and the nozzle marches outward into empty space one fused-plastic rung at a time. Two algorithms are bundled: **Anderson** (outward-expanding wavefronts, based on Janis A. Andersons' research) and **Kaiser LaSO** (lateral seed-curve offsetting, based on Rieks Kaiser's master thesis).

## How to enable

1. Open a model with an overhang.
2. Go to **Print Settings → Wave overhangs**.
3. Toggle **Use wave overhangs (Experimental)** on.
4. Pick an algorithm (Anderson or Kaiser).
5. Optionally pick a recipe (Balanced is a good starting point — named recipes auto-fill every underlying tunable).
6. Slice. Wave extrusions will appear in the G-code preview wherever overhangs are detected.

Simple mode only shows the 3 top-level controls (master toggle + algorithm + recipe). Switch the top-right mode selector to **Advanced** to see individual tunables. Editing any tunable automatically snaps the recipe dropdown to "Custom" so you don't silently override a preset.

---

## Tier 1 — Simple mode

Three controls, always visible.

### `wave_overhangs`

Master on/off switch. When off, none of the other wave-overhang settings have any effect and overhangs are printed with Orca's normal perimeter generator.

- **Type:** bool · **Default:** `false`

### `wave_overhang_algorithm`

Which generator produces the wave pattern.

- **Type:** enum — `anderson` | `kaiser` · **Default:** `anderson`

| Choice | What it does | Pick when |
|---|---|---|
| **Anderson (wavefront)** | Expands a wavefront outward from the supported edge in fixed-distance steps; each new ring anchors to the previous one. | Most geometries. Robust default, handles complex overhang shapes, good surface quality. |
| **Kaiser LaSO (lateral offset)** | Detects a seed curve at the root of the overhang and emits progressively offset rings laterally from it. | Long, fairly straight overhang ridges where you want a very regular, directional pattern. |

### `wave_overhang_recipe`

Preset bundle that fills every underlying tunable in one shot.

- **Type:** enum — `custom` | `balanced` | `aesthetic` | `structural` | `fast` · **Default:** `custom`

See [Recipe bundles](#recipe-bundles) below for the exact values each preset applies.

---

## Tier 2 — Advanced tuning

Every option below appears on the **Wave overhangs** page in Advanced mode, grouped by section.

### General

#### `wave_overhang_min_angle`

Soft/metadata-only slope threshold. **Currently not enforced** — retained on the profile for future use.

- **Type:** float (°) · **Default:** `0` · **Range:** `0 – 90`
- **Why it's inert:** the actual slope filter for what becomes an overhang is Orca's upstream *Strength → Detect overhang walls* + *Overhang reverse threshold* pipeline. By the time a region reaches the wave generator, it has already been classified as `erOverhangPerimeter`. A secondary local envelope check (earlier implementations) rejected every legitimate strip because the strip extends roughly one layer-height beyond the supported region by construction. If you want fewer or more overhangs flagged, adjust Orca's upstream thresholds instead.
- **What to do today:** leave it at `0`. Use *Overhang reverse threshold* (Strength tab) to control which walls the slicer considers overhangs.

#### `wave_overhang_min_length`

Minimum perimeter length (mm) of a detected overhang below which wave generation is skipped.

- **Type:** float (mm) · **Default:** `0.0` · **Range:** `0 – 50`
- **Tuning:** raise to 2–5 mm to avoid waving tiny overhang fragments (curved corners, small notches) where the travel/seam overhead isn't worth it.

### Geometry

#### `wave_overhang_outer_perimeters`

Number of additional concentric outer perimeters drawn inside the overhang region *before* the wave fill.

- **Type:** int · **Default:** `1` · **Range:** `0 – unbounded`
- **Tuning:** `1` is usually plenty. Raise to `2` for thicker outer shells on structural parts; drop to `0` for pure wave-only regions (experimental).

#### `wave_overhang_pattern`

Travel pattern between wave lines.

- **Type:** enum — `monotonic` | `zigzag` | `smart` · **Default:** `smart`

| Choice | Behavior |
|---|---|
| `monotonic` | Each line printed independently, same direction. Safest flow consistency, most travel. |
| `zigzag` | Lines connected into a back-and-forth meander. Minimum travel, but start-of-line is sometimes on the unsupported side. |
| `smart` | Starts each line from the better-supported end. Best default. |

#### `wave_overhang_perimeter_overlap`

Extends the wave propagation boundary outward toward kept perimeter lines so the outermost wave sits closer to the shell.

- **Type:** float (mm) · **Default:** `0.1` · **Range:** `0 – unbounded`
- **Tuning:** raise toward `0.2–0.3` if you see a visible gap between the last wave ring and the outer perimeter. Too high can cause wave lines to print *into* the perimeter.

#### `wave_overhang_narrow_split_threshold`

If a neck in the wave region is narrower than `threshold × line_spacing`, insert a thin split there before propagation (Anderson-only — the narrow-region handling from upstream alpha.6).

- **Type:** float (multiplier of line spacing) · **Default:** `2.0` · **Range:** `0 – unbounded`
- **Tuning:** raise to split more aggressively if wave rings skip over thin necks between lobes. Lower to `0` to disable splitting.

#### `wave_overhang_line_spacing`

Center-to-center distance between adjacent wave extrusions.

- **Type:** float (mm) · **Default:** `0.35` · **Min:** `0.01`
- **Tuning:** tighter (0.28–0.30) = denser fill, stronger, slower, risk of over-extrusion on cantilevers. Wider (0.40–0.50) = faster, visible gaps between rings, weaker.

#### `wave_overhang_line_width`

Extrusion width for wave lines.

- **Type:** float (mm) · **Default:** `0.4` · **Min:** `0.1`
- **Tuning:** typically match or slightly undercut the nozzle diameter. Narrower lines (0.35–0.38) cool faster and hold shape better on unsupported tips; wider lines are stronger but sag more.

#### `wave_overhang_spacing_mode`

How ring-to-ring step varies across the wave.

- **Type:** enum — `uniform` | `progressive` · **Default:** `uniform`
- `uniform` = constant step. `progressive` = tight at the supported root, widens toward the cantilever tip (better anchoring, slightly weaker tip).

#### `wave_overhang_seam_mode`

Direction pattern across successive rings.

- **Type:** enum — `alternating` | `aligned` | `random` · **Default:** `alternating`

| Choice | When to use |
|---|---|
| `alternating` | Boustrophedon (zig-zag). Minimum travel. Default. |
| `aligned` | Every ring same direction — more consistent flow, extra travel jumps between rings. |
| `random` | Scatters seam start points to hide the visible seam on show faces. |

#### `wave_overhang_direction_bias`

**Kaiser-only.** Rotates the wave-direction seed by this many degrees.

- **Type:** float (°) · **Default:** `0.0` · **Range:** `-90 – 90`
- `0` follows the natural root edge. Non-zero tilts the ring pattern — experimental, may push rings outside the overhang region.

### Anchoring

#### `wave_overhang_anchor_bite`

Distance the wave extends *into the supported region* past the overhang root.

- **Type:** float (mm) · **Default:** `1.0` · **Range:** `0 – 5`
- **Tuning:** raise (1.5–2.5) for tough filaments or cold beds where the first few rings are peeling. Lower (0.5) to minimize visible bite marks on the supported face.

#### `wave_overhang_anchor_passes`

Extra rings emitted starting from the supported edge before the main wave fan.

- **Type:** int · **Default:** `1` · **Range:** `0 – 5`
- **Tuning:** `0` disables. `1` default. `2–5` for extreme >70° overhangs where the foot needs reinforcement. Each pass adds plastic and time.

### Anderson-specific

These control the Anderson wavefront propagation. No effect when `wave_overhang_algorithm = kaiser`.

#### `wave_overhang_wavefront_advance`

Distance the wavefront advances per iteration.

- **Type:** float (mm) · **Default:** `0.7` · **Range:** `0.1 – 5`
- Andersons' reference value: 0.7 mm.
- **Tuning:** smaller (0.4–0.5) = more rings, smoother surface, longer print. Larger (1.0+) = fewer rings, rougher, faster.

#### `wave_overhang_discretization`

Sample spacing along each wavefront when emitting the next ring.

- **Type:** float (mm) · **Default:** `0.35` · **Range:** `0.05 – 2.0`
- Andersons' reference: 0.35 mm.
- **Tuning:** smaller = smoother arcs, heavier CPU. Larger = faster, can look polygonal.

#### `wave_overhang_anderson_max_iterations`

Safety cap on the number of wavefronts per overhang region.

- **Type:** int · **Default:** `0` (= unlimited) · **Range:** `0 – 500`
- Propagation stops naturally when no new area is being gained; this is a hard cap for pathological cases.

#### `wave_overhang_min_new_area`

Terminate propagation when a new wavefront adds less than this much new area.

- **Type:** float (mm²) · **Default:** `0.01` · **Range:** `0 – 100`
- Mode: Develop (not in Advanced page by default — expose via Develop mode).
- Andersons' reference: 1e-4 mm² (very small; our default is looser).
- **Tuning:** lower to keep propagating deep into tight regions; raise to terminate early on diminishing returns.

#### `wave_overhang_arc_resolution`

Arc-approximation segment count in Anderson wavefront geometry.

- **Type:** int · **Default:** `24` · **Range:** `4 – 128`
- Mode: Develop.
- Andersons' reference: 24.
- **Tuning:** rarely needs touching. Raise for visibly faceted arcs at macro scale; lower to shrink G-code.

### Kaiser LaSO

Only apply when `wave_overhang_algorithm = kaiser`.

#### `wave_overhang_laso_overlap`

Overlap fraction between successive Kaiser offset rings.

- **Type:** float · **Default:** `0.15` · **Range:** `0 – 0.9`
- **Tuning:** raise (0.25–0.35) for denser, more solid coverage. Lower (0.05–0.10) for faster, more open pattern with visible gaps.

#### `wave_overhang_kaiser_max_rings`

Safety cap on ring count per region.

- **Type:** int · **Default:** `0` (= unlimited) · **Range:** `0 – 500`
- Mirrors `wave_overhang_anderson_max_iterations` for the Kaiser side.

### Speed

#### `wave_overhang_print_speed`

Print speed for wave extrusions.

- **Type:** float (mm/s) · **Default:** `2.0` · **Min:** `0.1`
- **Tuning:** slower = better cooling and adhesion of cantilever rings. `1.5–2.5` mm/s is the usual useful range. Going above ~5 mm/s defeats the point of wave overhangs.

#### `wave_overhang_travel_speed`

Travel speed within wave regions between non-extruding hops.

- **Type:** float (mm/s) · **Default:** `40.0` · **Min:** `1.0`
- *Save-only in the current build — see [Known limitations](#known-limitations).*

### Cooling

#### `wave_overhang_fan_speed`

Part-cooling fan percentage forced during wave extrusions.

- **Type:** int (%) · **Default:** `100` · **Range:** `0 – 100`
- *Save-only in the current build — see [Known limitations](#known-limitations).*
- **Tuning:** keep at 100 % for PLA/PETG. Drop to 40–60 % for ABS/ASA to reduce warping, but note cantilever rings benefit hugely from fast cooling.

### Floor layers

#### `wave_overhang_floor_layers`

Number of solid floor layers placed directly above a wave region. **Authoritative**: this value overrides Orca's `bottom_shell_layers` behavior within the wave shadow — `N` means *exactly* N solid layers above the wave, not N-plus-whatever-bottom-shell-layers-adds.

- **Type:** int · **Default:** `2` · **Range:** `0 – 20`
- `N = 0` = zero solid layers above the wave footprint. The layer directly above the wave strip goes straight to sparse infill. Use for max material savings on purely aesthetic overhangs.
- `N = 2` (default) = two solid bridge/solid-infill layers above the wave before sparse infill resumes. Standard mechanical backing.
- `N = 3+` = heavier structural cap (slower, more filament, but stiffer).
- **Interaction with `bottom_shell_layers`:** inside the wave shadow, the floor_layers value wins. Outside the wave shadow, Orca's normal shell rules still apply. Implementation: each affected layer gets a `wave_overhang_shadow_polygons` mask that is subtracted from the bottom-shell seed set in both `discover_vertical_shells` and `discover_horizontal_shells`, preventing further solid propagation above N.

### Support integration

#### `support_remaining_areas_after_wave_overhangs`

When wave overhangs are on, generate supports only for overhang areas that *weren't* filled by wave toolpaths.

- **Type:** bool · **Default:** `false`
- Explicit support enforcers still apply normally.
- **Tuning:** turn on for conservative prints where wave coverage might miss edge cases — you get supports only where the wave algorithm gave up.

### Debug

#### `wave_overhang_debug_gcode`

Emit `;WAVE_OVERHANG_START` / `;WAVE_OVERHANG_END` comments around wave extrusions and `;WAVE_OVERHANG_CONFIG` banners at region boundaries.

- **Type:** bool · **Default:** `true`
- Comments only — no effect on the actual print. See [G-code markers](#g-code-markers) for format.

---

## Recipe bundles

Selecting a named recipe in `wave_overhang_recipe` rewrites every key below. Values are read from `Tab::on_value_change` in the current source.

| Key | Balanced | Aesthetic | Structural | Fast |
|---|---|---|---|---|
| `wave_overhang_outer_perimeters` | 1 | 2 | 2 | 1 |
| `wave_overhang_line_spacing` (mm) | 0.35 | 0.28 | 0.30 | 0.45 |
| `wave_overhang_line_width` (mm) | 0.40 | 0.38 | 0.42 | 0.45 |
| `wave_overhang_print_speed` (mm/s) | 2.0 | 1.5 | 2.0 | 4.0 |
| `wave_overhang_travel_speed` (mm/s) | 40 | 30 | 40 | 60 |
| `wave_overhang_fan_speed` (%) | 100 | 100 | 100 | 100 |
| `wave_overhang_laso_overlap` | 0.15 | 0.25 | 0.25 | 0.10 |
| `wave_overhang_floor_layers` | 2 | 2 | 3 | 1 |
| `wave_overhang_wavefront_advance` (mm) | 0.7 | 0.5 | 0.8 | 1.0 |
| `wave_overhang_discretization` (mm) | 0.35 | 0.25 | 0.35 | 0.5 |
| `wave_overhang_anderson_max_iterations` | 0 | 0 | 0 | 100 |
| `wave_overhang_min_new_area` (mm²) | 0.01 | 0.005 | 0.01 | 0.1 |
| `wave_overhang_arc_resolution` | 24 | 48 | 24 | 12 |

**Notes on what each preset aims for:**

- **Balanced** — sensible middle-ground. Use this first.
- **Aesthetic** — slow print, tight spacing, high arc resolution (48). Prioritizes surface finish.
- **Structural** — wider lines, more outer perimeters, thicker floor. Prioritizes mechanical strength.
- **Fast** — wider spacing, higher speeds, aggressive termination (min_new_area 0.1, capped at 100 iterations). Prioritizes print time.

**Custom** leaves every parameter untouched. Editing any of the keys above while a named preset is active automatically snaps the dropdown back to Custom.

---

## Known limitations

All user-facing options are now plumbed end-to-end. The table below notes mode exposure and any gotchas.

| Option | Status |
|---|---|
| `wave_overhang_print_speed` | Fully plumbed — per-path speed override. |
| `wave_overhang_travel_speed` | Fully plumbed — `GCodeWriter::travel_to_*` extended with optional per-move override, applied around wave extrusions. |
| `wave_overhang_fan_speed` | Fully plumbed — new `;_WAVE_OVERHANG_FAN_START/END` marker emitted around wave paths and handled in `CoolingBuffer` to drive the part-cooling fan percentage. |
| `wave_overhang_min_angle` | **Inert (save-only).** Kept on the profile but not enforced; Orca's upstream *Detect overhang walls* + *Overhang reverse threshold* (Strength tab) is the real slope filter. See key description above. |
| `support_remaining_areas_after_wave_overhangs` | Fully plumbed — residual polygons (wave-uncovered area) are collected and passed into Orca's support generation as enforcer regions. |
| `wave_overhang_min_new_area` | Anderson-only, Develop mode only. |
| `wave_overhang_arc_resolution` | Anderson-only, Develop mode only. |

Andersons-reference-parameter tunables (`wavefront_advance`, `discretization`, `anderson_max_iterations`, `min_new_area`, `arc_resolution`) only apply when the Anderson algorithm is selected. Kaiser-only tunables (`laso_overlap`, `kaiser_max_rings`, `direction_bias`) only apply when Kaiser is selected.

Kaiser's original post-processor places discrete pin-support nubs under overhangs. Those are **not** ported and not planned — use `support_remaining_areas_after_wave_overhangs` with Orca's normal supports instead.

---

## G-code markers

When `wave_overhang_debug_gcode = true` (the default), three kinds of comments appear in the output. They are pure comments — no motion, no effect on the print — but they're invaluable for verifying that waves were emitted and for post-processing tools that want to recognize wave regions.

**Region banner** (once per wave region, before any extrusion):

```
; WAVE_OVERHANG_CONFIG region=<N> algo=<anderson|kaiser> recipe=<name> outer_perim=<int> spacing=<mm> width=<mm> speed=<mm/s> travel=<mm/s> fan=<%> floor_layers=<int> min_angle=<deg> min_length=<mm> anchor_bite=<mm> anchor_passes=<int> laso_overlap=<frac> kaiser_max_rings=<int> direction_bias=<deg>
```

**Extrusion block markers** (wrap every wave extrusion):

```
; WAVE_OVERHANG_START
G1 X... Y... E...
...
; WAVE_OVERHANG_END
```

**Verifying waves were emitted:** slice your model, then grep the output for `WAVE_OVERHANG_START`. No matches means either Orca's upstream overhang-wall detection didn't flag any regions (check *Strength → Detect overhang walls* / *Overhang reverse threshold*), `wave_overhangs` is off, or your `min_length` threshold filtered everything. If you want the markers off for a production print, set `wave_overhang_debug_gcode = false`.
