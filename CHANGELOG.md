# Changelog

All notable changes to OrcaSlicer-WaveOverhangs relative to the OrcaSlicer upstream base.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning follows semver with pre-release tags (e.g. `v0.1.0-alpha.1`).

## [Unreleased]

### Added
- **First-launch config import** from OrcaSlicer, Bambu Studio and (detection-only for now) PrusaSlicer. New fork installs offer to copy existing printer / filament / process profiles so users migrating from stock Orca or Bambu don't have to reconfigure. Reachable any time from **File → Import → Import from other slicer**.
  - Source dirs probed per OS: `OrcaSlicer`, `BambuStudio`, `PrusaSlicer` under `$XDG_CONFIG_HOME` / `~/.config` (Linux), `~/Library/Application Support` (macOS), `%APPDATA%` (Windows).
  - OrcaSlicer + Bambu use the same JSON schema — copy is direct, never overwrites existing files, skips logs/caches/tmp.
  - PrusaSlicer is detected and surfaced in the dialog but flagged *"not yet supported"* until a follow-up PR lands the `.ini → .json` translator.

### Fixed — wave generation
- **Kaiser LaSO: rings now anchor to the supported edge instead of floating in air** (PR #6). Previously every ring was clipped to overhang-interior-only, stripping the root-anchor half of the loop and leaving tracks unsupported — exactly the "prints in air and fails" failure users reported. Rings can now extend into a thin anchor band of the supported lower slice (`max(2·line_width, anchor_bite)` wide), matching Kaiser's Python reference behaviour.
- **Kaiser LaSO: ring start-point follows last tool position** (PR #7). Ports Kaiser's `closest_point(xlast, ylast, points)` heuristic: each ring rotates (if closed) or flips (if open) so its start vertex is nearest to where the previous ring ended. Minimises travel moves and matches the reference's print order.
- **Kaiser LaSO: Shapely-compatible buffer + 0.1 mm seed densify** (PR #8). Seeds are resampled to 0.1 mm spacing before offsetting so round caps produce smooth arcs instead of faceted rings. Clipper's `arc_tolerance` set to `r × 0.019` to match Shapely's `resolution=8` quadrant segments at every ring radius.
- **`wave_overhang_outer_perimeters` tweaks actually take effect** (PR #5). The previous formula `additional_shell_count = max(0, desired - perimeter_count)` was always 0 because `desired` was capped at `perimeter_count` one line above — the config value was read but silently discarded. Corrected to `desired - 1` so extra shells beyond the innermost ring really do get emitted.

### Fixed — g-code / headers
- **Debug header bed temperature is now correct** (PR #2). OrcaSlicer stores bed temperature on per-bed-type keys (`hot_plate_temp`, `textured_plate_temp`, ...) rather than a single `bed_temperature` option. The emitter was reading the non-existent key and getting 0 for every upload. Resolves via `get_bed_temp_key(curr_bed_type)` / `get_bed_temp_1st_layer_key(curr_bed_type)`.
- **`WAVE_OVERHANG_BUILD` header slimmed** (PR #3). Orca already emits every print/printer/filament setting as `; key = value` at the tail of the g-code, so duplicating them in our custom header was noise. Trimmed to just `wave_overhangs_version` and `orca_base` — everything else (printer, filament, layer height, nozzle/bed temps, flow ratio, support flags) comes from Orca's own config block. The website parser's `parseSlicerConfigBlock` fallback reads those fields directly, so galleries keep working.

### Fixed — build / CI
- **Linux AppImage actually ships in releases** (PR #4). The build job renamed its output to `OrcaSlicerWaveOverhangs_Linux_AppImage…` but the `upload-artifact` step was still looking for the pre-rename `OrcaSlicer_Linux_AppImage…` path, so nothing got attached to the v0.1.0 or v0.1.1 releases. Fixed on future tags — v0.1.2 onwards will have Linux AppImages.

### Fixed
- **`wave_overhang_floor_layers = 0` truly produces zero solid layers above the wave.** Previous fix lived inside `detect_surfaces_type`, which runs before `process_external_surfaces`, `discover_vertical_shells`, and `discover_horizontal_shells` — those passes later re-introduced `stInternalSolid` / `stBottom` surfaces above the wave shadow, so users saw 2 "Internal solid infill" layers in the preview even with N=0. The authoritative promotion has been moved to a new `apply_wave_overhang_floor_layer_authority()` pass that runs AFTER every surface-classification pass, so its result cannot be overwritten.
- **No more `stBottomBridge` above wave strips.** The first layer above a wave was previously classified as `stBottomBridge` and rendered as "Internal bridge" in the preview. That's semantically wrong — the layer sits on top of the wave extrusions (solid material), not air. All N layers in the `wave_overhang_floor_layers` window are now uniformly `stInternalSolid` (regular solid infill).
- **`wave_overhang_floor_layers` is now authoritative, not additive.** Previously the N promoted layers were stacked ON TOP of Orca's default `bottom_shell_layers` (so N=5 silently became N=5+2=7). Now N=5 means *exactly* 5 solid layers above a wave region, and N=0 means zero solid layers (straight to sparse infill). Implementation: record a `wave_overhang_shadow_polygons` mask on every affected layer, then subtract that mask from the bottom-shell seed sets in `discover_vertical_shells` and `discover_horizontal_shells` so they don't propagate further solid layers above the wave footprint.
- **`wave_overhang_min_angle` filter deactivated.** The local `layer_height × tan(angle)` envelope test was over-eager and rejected every legitimate overhang (the incoming strip extends ~one layer-height beyond the support by construction, so it always matched). Orca's upstream `detect_overhang_wall` + `overhang_reverse_threshold` already performs the authoritative slope classification, so we trust that. The config key is preserved (default 0 = no filtering) for profile compatibility and potential future use; tooltip updated accordingly.

## v0.1.0-alpha.1 — first cross-platform alpha

### Added — feature
- **Wave overhangs** end-to-end. Steep cantilevered overhangs printed without supports by generating wave-patterned perimeters that anchor to previous passes.
- **Two algorithms with a dropdown.** Andersons (ported from stmcculloch's PrusaSlicer-WaveOverhangs — arc-overhang descendant with narrow-region splitting and smart/zig-zag/monotonic pattern selection) and Kaiser LaSO (C++ reimplementation of Rieks Kaiser's Python script with auto seed-curve detection and multi-overhang handling).
- **Dedicated Wave overhangs tab** in Print Settings with grouped sections: General · Geometry · Anchoring · Andersons · Kaiser LaSO · Speed · Cooling · Floor layers · Debug.
- **Algorithm-aware gating.** Andersons-only options hidden when Kaiser is selected, and vice versa. Every tunable hidden when the master toggle is off.
- **Floor-layer plumbing.** `wave_overhang_floor_layers` forces N layers above wave regions to be classified as `stBottomBridge` so they get solid fill instead of sparse infill.
- **G-code debug markers.** `;WAVE_OVERHANG_CONFIG` header block listing active wave settings per region, plus `;WAVE_OVERHANG_START/END` tags around wave extrusions. Enabled by default.
- **Public README + full settings reference** at `docs/WAVE_OVERHANG_SETTINGS.md`.
- **GitHub Actions** build triggers for the `wave-overhangs` branch; release workflow on `v*` tags produces Linux AppImage / Windows zip+installer / macOS universal DMG as draft GitHub Releases.

### Added — config options (29 new keys on `PrintRegionConfig`)
Master + algorithm:
- `wave_overhangs` (bool)
- `wave_overhang_algorithm` (enum: Andersons / Kaiser)

Shared tunables:
- `wave_overhang_outer_perimeters` (int)
- `wave_overhang_line_spacing` (float mm)
- `wave_overhang_line_width` (float mm)
- `wave_overhang_print_speed` (float mm/s)
- `wave_overhang_travel_speed` (float mm/s)
- `wave_overhang_fan_speed` (int %)
- `wave_overhang_floor_layers` (int)
- `wave_overhang_min_angle` (float °)
- `wave_overhang_min_length` (float mm)
- `wave_overhang_anchor_bite` (float mm)
- `wave_overhang_anchor_passes` (int)
- `wave_overhang_spacing_mode` (enum: uniform / progressive)
- `wave_overhang_seam_mode` (enum: alternating / aligned / random)
- `wave_overhang_debug_gcode` (bool — default true)
- `support_remaining_areas_after_wave_overhangs` (bool)

Andersons-only:
- `wave_overhang_pattern` (enum: smart / zigzag / monotonic)
- `wave_overhang_perimeter_overlap` (float)
- `wave_overhang_minimum_width` (float mm)
- `wave_overhang_wavefront_advance` (float mm)
- `wave_overhang_discretization` (float mm)
- `wave_overhang_andersons_max_iterations` (int)
- `wave_overhang_min_new_area` (float mm²)
- `wave_overhang_arc_resolution` (int)

Kaiser-only:
- `wave_overhang_laso_overlap` (float)
- `wave_overhang_kaiser_max_rings` (int)
- `wave_overhang_direction_bias` (float °, experimental)

### Fixed — build
- MPFR configure fails on GCC 15 Linux because GMP installs into `lib64/` — patched `deps/MPFR/MPFR.cmake` to pass explicit `--with-gmp-lib=lib64`, `--with-gmp-include`, `--libdir=lib64` on Linux only.
- wxWidgets static media lib needs `gstreamer-video-1.0` on Linux — added to `pkg_check_modules` and `target_link_libraries` in `src/slic3r/CMakeLists.txt`.
- `tmp/` added to `.gitignore` for local scratch.

### Added — plumbing (all previously save-only settings now apply)
- `wave_overhang_travel_speed` — `GCodeWriter::travel_to_*` extended with optional per-move speed; applied when the enclosing path has the wave-overhang flag.
- `wave_overhang_fan_speed` — new `;_WAVE_OVERHANG_FAN_START/END` marker emitted around wave paths and handled in `CoolingBuffer` to force the part-cooling fan percentage.
- `wave_overhang_min_angle` — per-region overhang-steepness estimator at the generator dispatcher; regions shallower than the threshold fall back to Orca's normal overhang path.
- `support_remaining_areas_after_wave_overhangs` — residual polygons (the area the wave couldn't cover) are passed into Orca's support generation as enforcer regions.

### Known limitations (also noted in README)
- Kaiser pin supports: not planned. Goal of this fork is fully support-free overhangs.
- Real-world print validation: Linux only so far (openSUSE Tumbleweed).

### Credits
- Wave overhangs algorithm: Janis A. Andersons (paper to be published; reference Python at [andersonsjanis/Wave-overhangs](https://github.com/andersonsjanis/Wave-overhangs))
- Arc-overhang algorithm and PrusaSlicer integration: Steven McCulloch ([stmcculloch](https://github.com/stmcculloch))
- Kaiser LaSO algorithm: Rieks Kaiser ([riekskaiser/wave_LaSO](https://github.com/riekskaiser/wave_LaSO))
- OrcaSlicer base: [OrcaSlicer/OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)
