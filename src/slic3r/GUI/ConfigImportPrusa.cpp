///|/ PrusaSlicer → OrcaSlicer config translator — see ConfigImportPrusa.hpp.
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#include "ConfigImportPrusa.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Slic3r { namespace GUI {
namespace fs = boost::filesystem;

// ---- Minimal INI reader ----------------------------------------------------

static std::map<std::string, std::string> parse_ini(const fs::path &path)
{
    std::map<std::string, std::string> out;
    std::ifstream in(path.string());
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        // Strip trailing \r for Windows-written files.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Comments + blank lines.
        auto ltrim = [](const std::string &s) {
            size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
            return s.substr(i);
        };
        const std::string t = ltrim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = t.substr(0, eq);
        std::string val = t.substr(eq + 1);
        // Trim key/value of surrounding spaces.
        auto trim = [](std::string &s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
            size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
            s.erase(0, i);
        };
        trim(key); trim(val);
        if (!key.empty()) out[key] = val;
    }
    return out;
}

// ---- Key translation tables ------------------------------------------------
// `Prusa key` → `Orca key`. Empty Orca key means identical name (we still
// want the key in the table so it counts as "mapped" for logging).

using KeyMap = std::unordered_map<std::string, std::string>;

// Keys shared between all profile types (ID / meta fields).
static const KeyMap COMMON_MAP = {
    {"inherits",       "inherits"},
    {"compatible_prints",   "compatible_prints"},
    {"compatible_printers", "compatible_printers"},
    {"print_settings_id",   ""},  // drop — Orca manages IDs itself
    {"notes",          "description"},
};

// print/*.ini → process/*.json
static const KeyMap PRINT_MAP = {
    {"layer_height",                   "layer_height"},
    {"first_layer_height",             "initial_layer_print_height"},
    {"perimeters",                     "wall_loops"},
    {"top_solid_layers",               "top_shell_layers"},
    {"bottom_solid_layers",            "bottom_shell_layers"},
    {"top_solid_min_thickness",        "top_shell_thickness"},
    {"bottom_solid_min_thickness",     "bottom_shell_thickness"},
    {"external_perimeter_speed",       "outer_wall_speed"},
    {"perimeter_speed",                "inner_wall_speed"},
    {"small_perimeter_speed",          "small_perimeter_speed"},
    {"infill_speed",                   "sparse_infill_speed"},
    {"solid_infill_speed",             "internal_solid_infill_speed"},
    {"top_solid_infill_speed",         "top_surface_speed"},
    {"bridge_speed",                   "bridge_speed"},
    {"gap_fill_speed",                 "gap_infill_speed"},
    {"travel_speed",                   "travel_speed"},
    {"first_layer_speed",              "initial_layer_speed"},
    {"fill_pattern",                   "sparse_infill_pattern"},
    {"fill_density",                   "sparse_infill_density"},
    {"top_fill_pattern",               "top_surface_pattern"},
    {"bottom_fill_pattern",            "bottom_surface_pattern"},
    {"infill_anchor",                  "infill_anchor"},
    {"infill_anchor_max",              "infill_anchor_max"},
    {"external_perimeters_first",      "outer_wall_first"},
    {"ensure_vertical_shell_thickness","ensure_vertical_shell_thickness"},
    {"avoid_crossing_perimeters",      "reduce_crossing_wall"},
    {"seam_position",                  "seam_position"},
    {"support_material",               "enable_support"},
    {"support_material_threshold",     "support_threshold_angle"},
    {"support_material_style",         "support_style"},
    {"support_material_pattern",       "support_base_pattern"},
    {"support_material_spacing",       "support_base_pattern_spacing"},
    {"support_material_speed",         "support_speed"},
    {"support_material_interface_layers", "support_interface_top_layers"},
    {"support_material_contact_distance", "support_top_z_distance"},
    {"brim_type",                      "brim_type"},
    {"brim_width",                     "brim_width"},
    {"skirts",                         "skirt_loops"},
    {"skirt_height",                   "skirt_height"},
    {"skirt_distance",                 "skirt_distance"},
    {"extra_perimeters",               "extra_perimeters_on_overhangs"},
    {"detect_bridging_perimeters",     "detect_overhang_wall"},
    {"bridge_flow_ratio",              "bridge_flow"},
    {"thin_walls",                     "detect_thin_wall"},
    {"perimeter_extrusion_width",      "inner_wall_line_width"},
    {"external_perimeter_extrusion_width", "outer_wall_line_width"},
    {"infill_extrusion_width",         "sparse_infill_line_width"},
    {"solid_infill_extrusion_width",   "internal_solid_infill_line_width"},
    {"top_infill_extrusion_width",     "top_surface_line_width"},
    {"extrusion_width",                "line_width"},
    {"first_layer_extrusion_width",    "initial_layer_line_width"},
    {"spiral_vase",                    "spiral_mode"},
    {"slice_closing_radius",           "slice_closing_radius"},
    {"resolution",                     "resolution"},
};

// filament/*.ini → filament/*.json
static const KeyMap FILAMENT_MAP = {
    {"filament_type",                  "filament_type"},
    {"filament_diameter",              "filament_diameter"},
    {"filament_density",               "filament_density"},
    {"filament_cost",                  "filament_cost"},
    {"filament_vendor",                "filament_vendor"},
    {"filament_colour",                "default_filament_colour"},
    {"extrusion_multiplier",           "filament_flow_ratio"},
    {"temperature",                    "nozzle_temperature"},
    {"first_layer_temperature",        "nozzle_temperature_initial_layer"},
    {"bed_temperature",                "hot_plate_temp"},
    {"first_layer_bed_temperature",    "hot_plate_temp_initial_layer"},
    {"fan_always_on",                  "reduce_fan_stop_start_freq"},
    {"min_fan_speed",                  "fan_min_speed"},
    {"max_fan_speed",                  "fan_max_speed"},
    {"bridge_fan_speed",               "overhang_fan_speed"},
    {"disable_fan_first_layers",       "close_fan_the_first_x_layers"},
    {"cooling",                        "slow_down_for_layer_cooling"},
    {"slowdown_below_layer_time",      "slow_down_layer_time"},
    {"min_print_speed",                "slow_down_min_speed"},
    {"fan_below_layer_time",           "fan_cooling_layer_time"},
    {"filament_max_volumetric_speed",  "filament_max_volumetric_speed"},
    {"start_filament_gcode",           "filament_start_gcode"},
    {"end_filament_gcode",             "filament_end_gcode"},
};

// printer/*.ini → machine/*.json
static const KeyMap PRINTER_MAP = {
    {"printer_model",                  "printer_model"},
    {"printer_vendor",                 "printer_vendor"},
    {"printer_variant",                "printer_variant"},
    {"printer_technology",             "printer_technology"},
    {"nozzle_diameter",                "nozzle_diameter"},
    {"bed_shape",                      "printable_area"},
    {"max_print_height",               "printable_height"},
    {"z_offset",                       "z_offset"},
    {"start_gcode",                    "machine_start_gcode"},
    {"end_gcode",                      "machine_end_gcode"},
    {"layer_gcode",                    "layer_change_gcode"},
    {"toolchange_gcode",               "change_filament_gcode"},
    {"before_layer_gcode",             "before_layer_change_gcode"},
    {"machine_max_acceleration_extruding",  "machine_max_acceleration_extruding"},
    {"machine_max_acceleration_retracting", "machine_max_acceleration_retracting"},
    {"machine_max_acceleration_travel",     "machine_max_acceleration_travel"},
    {"machine_max_speed_x",            "machine_max_speed_x"},
    {"machine_max_speed_y",            "machine_max_speed_y"},
    {"machine_max_speed_z",            "machine_max_speed_z"},
    {"machine_max_speed_e",            "machine_max_speed_e"},
    {"machine_max_jerk_x",             "machine_max_jerk_x"},
    {"machine_max_jerk_y",             "machine_max_jerk_y"},
    {"machine_max_jerk_z",             "machine_max_jerk_z"},
    {"machine_max_jerk_e",             "machine_max_jerk_e"},
    {"retract_length",                 "retraction_length"},
    {"retract_speed",                  "retraction_speed"},
    {"deretract_speed",                "deretraction_speed"},
    {"retract_before_travel",          "retraction_minimum_travel"},
    {"retract_lift",                   "z_hop"},
    {"retract_layer_change",           "retract_when_changing_layer"},
    {"wipe",                           "wipe"},
    {"use_firmware_retraction",        "use_firmware_retraction"},
    {"use_relative_e_distances",       "use_relative_e_distances"},
    {"single_extruder_multi_material", "single_extruder_multi_material"},
};

// ---- JSON writer ----------------------------------------------------------

// Minimal JSON encoder. Values from Prusa are already text; we escape and
// wrap as strings. Orca's loader tolerates numeric-looking strings, so this
// keeps the writer simple at the cost of a handful of bytes per file.
static std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static bool write_json(const fs::path                           &path,
                       const std::vector<std::pair<std::string,std::string>> &kv)
{
    boost::system::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path.string());
    if (!out) return false;
    out << "{\n";
    for (size_t i = 0; i < kv.size(); ++i) {
        out << "    \"" << json_escape(kv[i].first) << "\": \""
            << json_escape(kv[i].second) << "\"";
        if (i + 1 != kv.size()) out << ",";
        out << "\n";
    }
    out << "}\n";
    return out.good();
}

// ---- Per-profile-type translator ------------------------------------------

struct TranslationResult {
    std::vector<std::pair<std::string, std::string>> kv;
    std::vector<std::string>                          unmapped_keys;
};

static TranslationResult translate(const std::map<std::string,std::string> &ini,
                                   const KeyMap                             &map_primary,
                                   const std::string                        &profile_name,
                                   const std::string                        &from_label)
{
    TranslationResult r;
    r.kv.reserve(ini.size() + 4);
    r.kv.emplace_back("name", profile_name);
    r.kv.emplace_back("from", from_label);
    r.kv.emplace_back("version", "1.9.0.0");

    for (const auto &[k, v] : ini) {
        std::string ok;
        auto it = map_primary.find(k);
        if (it != map_primary.end())      ok = it->second.empty() ? k : it->second;
        else {
            auto cit = COMMON_MAP.find(k);
            if (cit != COMMON_MAP.end())  ok = cit->second.empty() ? k : cit->second;
        }
        if (ok.empty()) {
            r.unmapped_keys.push_back(k);
            continue;
        }
        r.kv.emplace_back(std::move(ok), v);
    }
    return r;
}

// ---- Main walker ----------------------------------------------------------

struct DirJob {
    std::string         subdir_in;
    std::string         subdir_out;
    const KeyMap       *map;
    std::string         from_label;
};

PrusaImportStats import_prusa(const fs::path &prusa_root, const fs::path &orca_root)
{
    PrusaImportStats stats;
    stats.log_path = orca_root / "config-import-prusa.log";

    boost::system::error_code ec;
    std::ofstream log(stats.log_path.string());

    const fs::path user_out = orca_root / "user" / "default";
    const DirJob jobs[] = {
        { "print",    "process",  &PRINT_MAP,    "PrusaSlicer (print)" },
        { "filament", "filament", &FILAMENT_MAP, "PrusaSlicer (filament)" },
        { "printer",  "machine",  &PRINTER_MAP,  "PrusaSlicer (printer)" },
    };

    for (const auto &job : jobs) {
        fs::path src_dir = prusa_root / job.subdir_in;
        if (!fs::is_directory(src_dir, ec)) continue;
        fs::path dst_dir = user_out / job.subdir_out;

        for (fs::directory_iterator it(src_dir, ec), end; it != end && !ec; ++it) {
            if (!fs::is_regular_file(it->path(), ec)) continue;
            if (it->path().extension() != ".ini")    continue;

            std::string stem = it->path().stem().string();
            fs::path    out  = dst_dir / (stem + ".json");
            if (fs::exists(out, ec)) continue; // never overwrite

            auto ini = parse_ini(it->path());
            if (ini.empty()) { ++stats.files_skipped; continue; }

            auto result = translate(ini, *job.map, stem, job.from_label);

            if (!write_json(out, result.kv)) { ++stats.files_skipped; continue; }
            ++stats.files_written;
            stats.keys_translated += int(result.kv.size()) - 3; // minus name/from/version

            if (!result.unmapped_keys.empty() && log.is_open()) {
                log << "[" << job.subdir_in << "/" << stem << ".ini] "
                    << result.unmapped_keys.size() << " unmapped keys: ";
                for (size_t i = 0; i < result.unmapped_keys.size(); ++i) {
                    if (i) log << ", ";
                    log << result.unmapped_keys[i];
                }
                log << "\n";
                stats.keys_unmapped += int(result.unmapped_keys.size());
            }
        }
    }

    BOOST_LOG_TRIVIAL(info)
        << "config-import prusa: wrote " << stats.files_written
        << " files, translated " << stats.keys_translated << " keys, skipped "
        << stats.keys_unmapped << " unmapped, " << stats.files_skipped
        << " files failed";
    return stats;
}

}} // namespace Slic3r::GUI
