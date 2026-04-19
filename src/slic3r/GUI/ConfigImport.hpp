///|/ First-launch config importer: detects sibling slicer config directories
///|/ (stock OrcaSlicer, Bambu Studio, PrusaSlicer) and offers to copy their
///|/ printers / filaments / print profiles into our own data dir so users
///|/ don't have to re-configure the fork from scratch.
///|/
///|/ PR A: OrcaSlicer + Bambu Studio (same JSON format — straight tree copy).
///|/ PR B (follow-up): PrusaSlicer (.ini → JSON translator).
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#ifndef slic3r_GUI_ConfigImport_hpp_
#define slic3r_GUI_ConfigImport_hpp_

#include <string>
#include <vector>
#include <boost/filesystem/path.hpp>

namespace Slic3r { namespace GUI {

enum class ImportSource {
    OrcaSlicer,
    BambuStudio,
    PrusaSlicer,   // detected but not yet supported by this PR's copier
};

struct ImportCandidate {
    ImportSource              source;
    boost::filesystem::path   path;
    std::string               label;          // user-facing, e.g. "OrcaSlicer"
    std::uintmax_t            size_bytes = 0;
    int                       printer_profiles = 0;
    bool                      supported_now = true;
};

// Probe the standard config paths for every supported source on this OS.
// Returns only sources whose directory exists and isn't empty.
std::vector<ImportCandidate> detect_import_candidates();

// Present the first-launch dialog. If the user picks a source and confirms,
// copy it into the current data_dir() and return true. Returns false on
// skip / close / error.
bool run_import_dialog(const std::vector<ImportCandidate> &candidates);

// Exposed for the File → Import menu item — re-runs detection + dialog even
// when the data dir isn't empty.
bool run_import_from_menu();

// Directory copy used by OrcaSlicer/Bambu import. Skips caches, logs, and
// obvious non-config junk.
bool copy_config_tree(const boost::filesystem::path &src,
                      const boost::filesystem::path &dst,
                      std::string                   *out_error = nullptr);

}} // namespace Slic3r::GUI

#endif
