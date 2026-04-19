///|/ PrusaSlicer → OrcaSlicer config translator.
///|/
///|/ PrusaSlicer stores profiles as .ini files under print/, filament/,
///|/ printer/ subdirectories of its data dir. OrcaSlicer uses .json files
///|/ with a different schema under user/<id>/{process,filament,machine}/.
///|/ This module reads the .ini files, translates the keys we know about
///|/ into Orca equivalents, writes JSON in Orca's layout, and logs any keys
///|/ we couldn't map so the user can see what didn't carry over.
///|/
///|/ Scope of this first version (PR B): ~80 common keys per profile type.
///|/ Enough for most prints to slice. Exotic settings (advanced wipe tower,
///|/ complex physical-printer configs, connector profiles) are not mapped.
///|/
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#ifndef slic3r_GUI_ConfigImportPrusa_hpp_
#define slic3r_GUI_ConfigImportPrusa_hpp_

#include <boost/filesystem/path.hpp>

namespace Slic3r { namespace GUI {

struct PrusaImportStats {
    int files_written    = 0;   // output .json files
    int keys_translated  = 0;
    int keys_unmapped    = 0;   // Prusa key with no Orca counterpart in our table
    int files_skipped    = 0;   // ini files that failed to parse
    boost::filesystem::path log_path; // unmapped-key summary for the user
};

// Walk every .ini under `prusa_root/{print,filament,printer}/` and write
// translated .json files under `orca_root/user/default/{process,filament,machine}/`.
// Skips Orca config files that already exist — never overwrites.
PrusaImportStats import_prusa(const boost::filesystem::path &prusa_root,
                              const boost::filesystem::path &orca_root);

}} // namespace Slic3r::GUI

#endif
