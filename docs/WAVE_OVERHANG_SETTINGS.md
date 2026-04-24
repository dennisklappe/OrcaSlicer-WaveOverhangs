# Wave Overhang Settings Reference

This document describes every config option added by the wave-overhangs fork of OrcaSlicer, what it does, and how to tune it. For an overview of what wave overhangs are and why they exist, see the [main README](../README.md).

## Contents

1. [What wave overhangs are](#what-wave-overhangs-are)
2. [How to enable](#how-to-enable)
3. [Tier 1: Simple mode](#tier-1-simple-mode)
4. [Tier 2: Advanced tuning](#tier-2-advanced-tuning)
   - [General](#general)
   - [Geometry](#geometry)
   - [Anchoring](#anchoring)
   - [Andersons-specific](#andersons-specific)
   - [Kaiser LaSO](#kaiser-laso)
   - [Speed](#speed)
   - [Cooling](#cooling)
   - [Floor layers](#floor-layers)
   - [Support integration](#support-integration)
   - [Debug](#debug)
5. [Known limitations](#known-limitations)
6. [G-code markers](#g-code-markers)

---

## What wave overhangs are

Wave overhangs let you print steep cantilevered overhangs without supports. Instead of dropping support columns from the bed, each ring of extrusion anchors to the one before it and the nozzle marches outward into empty space one fused-plastic rung at a time. Two algorithms are bundled: **Andersons** (outward-expanding wavefronts, based on Janis A. Andersons' research) and **Kaiser LaSO** (lateral seed-curve offsetting, based on Rieks Kaiser's master thesis).

## How to enable

1. Open a model with an overhang.
2. Go to **Print Settings → Wave overhangs**.
3. Toggle **Use wave overhangs (Experimental)** on.
4. Pick an algorithm (Andersons or Kaiser).
5. Slice. Wave extrusions will appear in the G-code preview wherever overhangs are detected.

Simple mode only shows the 2 top-level controls (master toggle + algorithm). Switch the top-right mode selector to **Advanced** to see individual tunables.

---

## Tier 1: Simple mode

Two controls, always visible.

### `wave_overhangs`

Master on/off switch. When off, none of the other wave-overhang settings have any effect and overhangs are printed with Orca's normal perimeter generator.

- **Type:** bool · **Default:** `false`

### `wave_overhang_algorithm`

Which generator produces the wave pattern.

- **Type:** enum (`andersons` | `kaiser`) · **Default:** `andersons`

| Choice | What it does | Pick when |
|---|---|---|
| **Andersons (wavefront)** | Expands a wavefront outward from the supported edge in fixed-distance steps; each new ring anchors to the previous one. | Most geometries. Robust default, handles complex overhang shapes, good surface quality. |
| **Kaiser LaSO (lateral offset)** | Detects a seed curve at the root of the overhang and emits progressively offset rings laterally from it. | Long, fairly straight overhang ridges where you want a very regular, directional pattern. |

> **No presets yet.** The tunable space is large and the "right" bundles depend on printer, material, and geometry. Rather than ship opinionated defaults now, we want community test prints to surface what actually works. Expect presets to return once there's real data.

---

## Tier 2: Advanced tuning

Every option below appears on the **Wave overhangs** page in Advanced mode, grouped by section.

### General

#### `wave_overhang_min_angle`

Soft/metadata-only slope threshold. **Currently not enforced**; retained on the profile for future use.

- **Type:** float (°) · **Default:** `0` · **Range:** `0 to 90`
- **Why it's inert:** the actual slope filter for what becomes an overhang is Orca's upstream *Strength → Detect overhang walls* + *Overhang reverse threshold* pipeline. By the time a region reaches the wave generator, it has already been classified as `erOverhangPerimeter`. A secondary local envelope check (earlier implementations) rejected every legitimate strip because the strip extends roughly one layer-height beyond the supported region by construction. If you want fewer or more overhangs flagged, adjust Orca's upstream thresholds instead.
- **What to do today:** leave it at `0`. Use *Overhang reverse threshold* (Strength tab) to control which walls the slicer considers overhangs.

#### `wave_overhang_min_length`

Minimum perimeter length (mm) of a detected overhang below which wave generation is skipped.

- **Type:** float (mm) · **Default:** `0.0` · **Range:** `0 to 50`
- **Tuning:** raise to 2 to 5 mm to avoid waving tiny overhang fragments (curved corners, small notches) where the travel/seam overhead isn't worth it.

### Geometry

#### `wave_overhang_outer_perimeters`

Number of additional concentric outer perimeters drawn inside the overhang region *before* the wave fill.

- **Type:** int · **Default:** `1` · **Range:** `0 to unbounded`
- **Tuning:** `1` is usually plenty. Raise to `2` for thicker outer shells on structural parts; drop to `0` for pure wave-only regions (experimental).

#### `wave_overhang_pattern`

Travel pattern between wave lines.

- **Type:** enum (`monotonic` | `zigzag` | `smart`) · **Default:** `smart`

| Choice | Behavior |
|---|---|
| `monotonic` | Each line printed independently, same direction. Safest flow consistency, most travel. |
| `zigzag` | Lines connected into a back-and-forth meander. Minimum travel, but start-of-line is sometimes on the unsupported side. |
| `smart` | Starts each line from the better-supported end. Best default. |

#### `wave_overhang_perimeter_overlap`

Extends the wave propagation boundary outward toward kept perimeter lines so the outermost wave sits closer to the shell.

- **Type:** float (mm) · **Default:** `0.1` · **Range:** `0 to unbounded`
- **Tuning:** raise toward `0.2 to 0.3` if you see a visible gap between the last wave ring and the outer perimeter. Too high can cause wave lines to print *into* the perimeter.

#### `wave_overhang_minimum_width`

If a neck in the wave region is narrower than this width, insert a thin split there before propagation so fragile wave branches do not form. Andersons-only; the narrow-region handling from upstream alpha.6, switched to an absolute width in upstream master.

- **Type:** float (mm) · **Default:** `0.7` · **Range:** `0 to unbounded`
- **Tuning:** raise to split more aggressively if wave rings skip over thin necks between lobes. Lower to `0` to disable splitting.

#### `wave_overhang_line_spacing`

Center-to-center distance between adjacent wave extrusions.

- **Type:** float (mm) · **Default:** `0.35` · **Min:** `0.01`
- **Tuning:** tighter (0.28 to 0.30) = denser fill, stronger, slower, risk of over-extrusion on cantilevers. Wider (0.40 to 0.50) = faster, visible gaps between rings, weaker.

#### `wave_overhang_flow_mm3_per_mm`

Absolute volume of plastic extruded per millimetre of wave-overhang line. Applied uniformly by both Andersons and Kaiser; replaces the old `wave_overhang_flow_ratio`.

- **Type:** float (mm³/mm) · **Default:** `0.16` · **Range:** `0.02 to 1.5`
- **Why absolute (not a ratio):** a wave-overhang line hangs in air, not squished against a layer below. Nothing to squish into means layer height has no effect on the bead's cross-section; the bead size is set by nozzle bore and mm³/mm extrusion rate alone. An absolute mm³/mm captures that directly.
- **Why 0.16:** equals `nozzle²` for a 0.4 mm nozzle, which matches Kaiser's reference calibration and also matches the old `flow_ratio = 2.0` at 0.2 mm layer height.
- **For other nozzle sizes:** use `nozzle_diameter²` as a starting point. 0.3 mm → 0.09, 0.5 mm → 0.25, 0.6 mm → 0.36, 0.8 mm → 0.64. The setting is not automatically adjusted when you change the printer profile, so update it by hand if you move between nozzle sizes.

**Tuning guide:**
- Start at `0.16` (or `nozzle²` for your nozzle) and print.
- If wave lines look thin / starved / broken: raise by 10 to 20 % (e.g. 0.18 to 0.20 for a 0.4 mm nozzle).
- If wave lines blob or merge: lower by 10 to 20 % (e.g. 0.13 to 0.14).

#### `wave_overhang_end_retract_length`

Forced retraction (in mm) emitted at the end of every wave-overhang line. Independent of the filament's normal retraction settings.

- **Type:** float (mm) · **Default:** `0.0` · **Range:** `0 to 10`
- **Why:** wave lines finish in mid-air, so residual nozzle pressure dribbles a small blob that sticks to the next travel path or piles up against the enclosing perimeter. Orca's normal retraction is travel-distance-gated; short inter-wave travels often don't cross the threshold, so most wave line ends never retract.
- **What it does:** after each wave line, emits `G1 E-<length> F...` (labelled `; WAVE_OVERHANG_END` in the G-code). The next wave line's lead-in travel automatically unretracts via the filament's existing state tracking.
- **Tuning:** `0` = disabled, use normal retraction heuristic (current behaviour for existing profiles). Start at `0.4 to 0.8 mm` if you see end-of-line blobs or over-extrusion piled against the outer perimeter; raise until the artifacts go away. Above `1.5 mm` you risk under-extrusion at the start of the next line.

#### `wave_overhang_spacing_mode`

How ring-to-ring step varies across the wave.

- **Type:** enum (`uniform` | `progressive`) · **Default:** `uniform`
- `uniform` = constant step. `progressive` = tight at the supported root, widens toward the cantilever tip (better anchoring, slightly weaker tip).

#### `wave_overhang_seam_mode`

Direction pattern across successive rings.

- **Type:** enum (`alternating` | `aligned` | `random`) · **Default:** `alternating`

| Choice | When to use |
|---|---|
| `alternating` | Boustrophedon (zig-zag). Minimum travel. Default. |
| `aligned` | Every ring same direction, for more consistent flow, at the cost of extra travel jumps between rings. |
| `random` | Scatters seam start points to hide the visible seam on show faces. |

### Andersons-specific

These control the Andersons wavefront propagation. No effect when `wave_overhang_algorithm = kaiser`.

#### `wave_overhang_min_new_area`

Terminate propagation when a new wavefront adds less than this much new area.

- **Type:** float (mm²) · **Default:** `0.01` · **Range:** `0 to 100`
- Mode: Develop (not in Advanced page by default; expose via Develop mode).
- Andersons' reference: 1e-4 mm² (very small; our default is looser).
- **Tuning:** lower to keep propagating deep into tight regions; raise to terminate early on diminishing returns.

### Kaiser LaSO

Only apply when `wave_overhang_algorithm = kaiser`.

#### `wave_overhang_ring_overlap`

Overlap fraction between successive Kaiser offset rings.

- **Type:** float · **Default:** `0.15` · **Range:** `0 to 0.9`
- **Tuning:** raise (0.25 to 0.35) for denser, more solid coverage. Lower (0.05 to 0.10) for faster, more open pattern with visible gaps.

### Max iterations (shared safety cap)

#### `wave_overhang_max_iterations`

Safety cap on the algorithm's main loop: max wavefronts for Andersons, max rings for Kaiser.

- **Type:** int · **Default:** `0` (= unlimited) · **Range:** `0 to 500`
- Both algorithms stop naturally when they can't grow further; this is a hard cap for pathological cases or to bound print time on very large overhangs.

### Speed

#### `wave_overhang_print_speed`

Print speed for wave extrusions.

- **Type:** float (mm/s) · **Default:** `2.0` · **Min:** `0.1`
- **Tuning:** slower = better cooling and adhesion of cantilever rings. `1.5 to 2.5` mm/s is the usual useful range. Going above ~5 mm/s defeats the point of wave overhangs.

#### `wave_overhang_travel_speed`

Travel speed within wave regions between non-extruding hops.

- **Type:** float (mm/s) · **Default:** `40.0` · **Min:** `1.0`
- *Save-only in the current build; see [Known limitations](#known-limitations).*

### Cooling

#### `wave_overhang_fan_speed`

Part-cooling fan percentage forced during wave extrusions.

- **Type:** int (%) · **Default:** `100` · **Range:** `0 to 100`
- *Save-only in the current build; see [Known limitations](#known-limitations).*
- **Tuning:** keep at 100 % for PLA/PETG. Drop to 40 to 60 % for ABS/ASA to reduce warping, but note cantilever rings benefit hugely from fast cooling.

### Floor layers

#### `wave_overhang_floor_layers`

Number of solid floor layers placed directly above a wave region. **Authoritative**: this value overrides Orca's `bottom_shell_layers` behavior within the wave shadow. `N` means *exactly* N solid layers above the wave, not N-plus-whatever-bottom-shell-layers-adds.

- **Type:** int · **Default:** `2` · **Range:** `0 to 20`
- `N = 0` = zero solid layers above the wave footprint. The layer directly above the wave strip goes straight to sparse infill. Use for max material savings on purely aesthetic overhangs.
- `N = 2` (default) = two solid-infill layers above the wave before sparse infill resumes. Standard mechanical backing.
- `N = 3+` = heavier structural cap (slower, more filament, but stiffer).
- **All N layers are `stInternalSolid` (regular solid infill); no bridge classification is used, since these layers sit on top of the wave extrusions (which are solid material) rather than spanning air.** Layer L+1 was previously misclassified as `stBottomBridge` and rendered as "Internal bridge" in the preview; that has been fixed so the whole floor window is uniform solid infill.
- **Interaction with `bottom_shell_layers`:** inside the wave shadow, the floor_layers value wins. Outside the wave shadow, Orca's normal shell rules still apply. Implementation: each affected layer gets a `wave_overhang_shadow_polygons` mask that is subtracted from the bottom-shell seed set in both `discover_vertical_shells` and `discover_horizontal_shells`, preventing further solid propagation above N.

### Support integration

#### `support_remaining_areas_after_wave_overhangs`

When wave overhangs are on, generate supports only for overhang areas that *weren't* filled by wave toolpaths.

- **Type:** bool · **Default:** `false`
- Explicit support enforcers still apply normally.
- **Tuning:** turn on for conservative prints where wave coverage might miss edge cases; you get supports only where the wave algorithm gave up.

### Debug

#### `wave_overhang_debug_gcode`

Emit `;WAVE_OVERHANG_START` / `;WAVE_OVERHANG_END` comments around wave extrusions and `;WAVE_OVERHANG_CONFIG` banners at region boundaries.

- **Type:** bool · **Default:** `true`
- Comments only; no effect on the actual print. See [G-code markers](#g-code-markers) for format.

---

## Known limitations

All user-facing options are now plumbed end-to-end. The table below notes mode exposure and any gotchas.

| Option | Status |
|---|---|
| `wave_overhang_print_speed` | Fully plumbed; per-path speed override. |
| `wave_overhang_travel_speed` | Fully plumbed; `GCodeWriter::travel_to_*` extended with optional per-move override, applied around wave extrusions. |
| `wave_overhang_fan_speed` | Fully plumbed; new `;_WAVE_OVERHANG_FAN_START/END` marker emitted around wave paths and handled in `CoolingBuffer` to drive the part-cooling fan percentage. |
| `wave_overhang_min_angle` | **Inert (save-only).** Kept on the profile but not enforced; Orca's upstream *Detect overhang walls* + *Overhang reverse threshold* (Strength tab) is the real slope filter. See key description above. |
| `support_remaining_areas_after_wave_overhangs` | Fully plumbed; residual polygons (wave-uncovered area) are collected and passed into Orca's support generation as enforcer regions. |
| `wave_overhang_min_new_area` | Andersons-only, Develop mode only. |

Andersons-only tunables (`min_new_area`, plus wavefront-geometry knobs like `pattern`, `perimeter_overlap`, `minimum_width`, `line_spacing`, `spacing_mode`) only apply when the Andersons algorithm is selected. Kaiser-only tunables (`ring_overlap`) only apply when Kaiser is selected. The shared safety cap `max_iterations` applies to both.

Kaiser's original post-processor places discrete pin-support nubs under overhangs. Those are **not** ported and not planned; use `support_remaining_areas_after_wave_overhangs` with Orca's normal supports instead.

---

## G-code markers

When `wave_overhang_debug_gcode = true` (the default), four kinds of comments appear in the output. They are pure comments (no motion, no effect on the print), but they're invaluable for verifying that waves were emitted and for bug reports (the full wave setup can be reconstructed from the header alone).

**Build context** (once, before any region banners):

```
; WAVE_OVERHANG_BUILD printer="<model>" printer_variant="<nozzle/variant>"
  filament_type=<PLA|PETG|...>
  layer_height=<mm> initial_layer_height=<mm>
  nozzle_diameter=<mm>
  nozzle_temp=<C> nozzle_temp_initial=<C>
  filament_flow_ratio=<ratio>
```

(Emitted as a single line; split here for readability.)

**Region banner** (once per wave region, before any extrusion):

```
; WAVE_OVERHANG_CONFIG region=<N> algo=<andersons|kaiser> outer_perim=<int>
  spacing=<mm> width=<mm> flow_mm3_per_mm=<x> speed=<mm/s> travel=<mm/s> fan=<%>
  floor_layers=<int> min_angle=<deg> min_length=<mm> max_iterations=<int>
  ring_overlap=<frac>
  pattern=<smart|monotonic|zigzag> spacing_mode=<uniform|progressive> seam_mode=<alternating|aligned|random>
  perimeter_overlap=<mm> minimum_wave_width=<mm>
  min_new_area=<mm²>
  support_remainder=<0|1>
```

(Emitted as a single line in the G-code; split across lines here only for readability.)

**Extrusion block markers** (wrap every wave extrusion):

```
; WAVE_OVERHANG_START
G1 X... Y... E...
...
; WAVE_OVERHANG_END
```

**Verifying waves were emitted:** slice your model, then grep the output for `WAVE_OVERHANG_START`. No matches means either Orca's upstream overhang-wall detection didn't flag any regions (check *Strength → Detect overhang walls* / *Overhang reverse threshold*), `wave_overhangs` is off, or your `min_length` threshold filtered everything. If you want the markers off for a production print, set `wave_overhang_debug_gcode = false`.
