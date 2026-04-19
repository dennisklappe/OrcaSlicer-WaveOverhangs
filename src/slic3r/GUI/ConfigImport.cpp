///|/ Config importer — see ConfigImport.hpp.
///|/ Released under the terms of the AGPLv3 or higher.
///|/
#include "ConfigImport.hpp"
#include "ConfigImportPrusa.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <sstream>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

namespace Slic3r { namespace GUI {
namespace fs = boost::filesystem;

// ---- OS-specific probe paths -----------------------------------------------

static std::vector<fs::path> config_root_candidates()
{
    // Returns the base directories that might contain per-slicer config dirs
    // (e.g. on Linux $XDG_CONFIG_HOME, falling back to ~/.config).
    std::vector<fs::path> out;
#ifdef _WIN32
    // %APPDATA%  e.g. C:\Users\foo\AppData\Roaming
    wxString appdata = wxStandardPaths::Get().GetUserDataDir();
    // GetUserDataDir() on Windows returns .../AppData/Roaming/<AppName>.
    // Strip the app-name segment to get the shared root.
    fs::path p = fs::path(appdata.ToUTF8().data()).parent_path();
    out.push_back(p);
#elif defined(__APPLE__)
    // ~/Library/Application Support
    wxString app_sup = wxStandardPaths::Get().GetUserDataDir();
    fs::path p = fs::path(app_sup.ToUTF8().data()).parent_path();
    out.push_back(p);
#else
    // Linux: XDG_CONFIG_HOME, fall back to ~/.config
    wxString xdg;
    if (wxGetEnv(wxS("XDG_CONFIG_HOME"), &xdg) && !xdg.empty())
        out.push_back(fs::path(xdg.ToUTF8().data()));
    else
        out.push_back(fs::path((wxFileName::GetHomeDir() + wxS("/.config")).ToUTF8().data()));
#endif
    return out;
}

static std::uintmax_t dir_size(const fs::path &p)
{
    std::uintmax_t total = 0;
    boost::system::error_code ec;
    for (fs::recursive_directory_iterator it(p, ec), end; it != end && !ec; it.increment(ec)) {
        if (ec) break;
        if (fs::is_regular_file(it->path(), ec)) total += fs::file_size(it->path(), ec);
    }
    return total;
}

static int count_printer_profiles(const fs::path &root, ImportSource src)
{
    boost::system::error_code ec;
    int n = 0;
    if (src == ImportSource::PrusaSlicer) {
        fs::path printers = root / "printer";
        if (fs::is_directory(printers, ec)) {
            for (fs::directory_iterator it(printers, ec), end; it != end && !ec; ++it)
                if (fs::is_regular_file(it->path(), ec) && it->path().extension() == ".ini") ++n;
        }
    } else {
        // Orca / Bambu store user printers under user/<id>/machine/*.json
        fs::path user = root / "user";
        if (!fs::is_directory(user, ec)) return 0;
        for (fs::directory_iterator it(user, ec), end; it != end && !ec; ++it) {
            fs::path machine = it->path() / "machine";
            if (!fs::is_directory(machine, ec)) continue;
            for (fs::directory_iterator mi(machine, ec), mend; mi != mend && !ec; ++mi)
                if (fs::is_regular_file(mi->path(), ec) && mi->path().extension() == ".json") ++n;
        }
    }
    return n;
}

std::vector<ImportCandidate> detect_import_candidates()
{
    const auto roots = config_root_candidates();
    struct Spec { ImportSource src; std::string dir_name; std::string label; bool supported; };
    const std::vector<Spec> specs = {
        { ImportSource::OrcaSlicer,  "OrcaSlicer",   "OrcaSlicer",                 true },
        { ImportSource::BambuStudio, "BambuStudio",  "Bambu Studio",               true },
        { ImportSource::PrusaSlicer, "PrusaSlicer",  "PrusaSlicer (partial)",      true },
    };
    std::vector<ImportCandidate> found;
    for (const auto &root : roots) {
        for (const Spec &s : specs) {
            fs::path p = root / s.dir_name;
            boost::system::error_code ec;
            if (!fs::is_directory(p, ec)) continue;
            ImportCandidate c;
            c.source           = s.src;
            c.path             = p;
            c.label            = s.label;
            c.size_bytes       = dir_size(p);
            c.printer_profiles = count_printer_profiles(p, s.src);
            c.supported_now    = s.supported;
            // Skip empty/near-empty dirs (<1 MB, <1 profile) — likely stale.
            if (c.size_bytes < 1024 * 1024 && c.printer_profiles == 0) continue;
            found.push_back(std::move(c));
        }
    }
    return found;
}

// ---- Copy tree (Orca/Bambu) ------------------------------------------------

// Files/dirs we deliberately don't copy: logs, caches, runtime state. The
// remaining content (vendor profiles, user/ tree, appconfig.cfg) is what
// Orca actually reads on startup.
static bool path_is_skippable(const fs::path &rel)
{
    const std::string s = rel.generic_string();
    static const std::vector<std::string> skips = {
        "log", "logs", "cache", "gcode-preview-cache",
        "printer_presets", "crash_logs", "tmp", "download",
        "shapes/thumbnails",
    };
    for (const auto &k : skips) {
        if (s == k || s.rfind(k + "/", 0) == 0) return true;
    }
    return false;
}

bool copy_config_tree(const fs::path &src, const fs::path &dst, std::string *out_error)
{
    boost::system::error_code ec;
    if (!fs::is_directory(src, ec)) {
        if (out_error) *out_error = "source is not a directory";
        return false;
    }
    if (!fs::exists(dst, ec)) fs::create_directories(dst, ec);

    for (fs::recursive_directory_iterator it(src, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        fs::path rel = fs::relative(it->path(), src, ec);
        if (ec) { ec.clear(); continue; }
        if (path_is_skippable(rel)) {
            if (fs::is_directory(it->path(), ec)) it.disable_recursion_pending();
            continue;
        }
        fs::path target = dst / rel;
        if (fs::is_directory(it->path(), ec)) {
            if (!fs::exists(target, ec)) fs::create_directories(target, ec);
        } else if (fs::is_regular_file(it->path(), ec)) {
            // Don't overwrite files the user already has.
            if (fs::exists(target, ec)) continue;
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(it->path(), target, fs::copy_option::overwrite_if_exists, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(warning) << "config-import: failed to copy " << it->path() << " -> " << target << ": " << ec.message();
                ec.clear();
            }
        }
    }
    return true;
}

// ---- Dialog ----------------------------------------------------------------

static std::string human_size(std::uintmax_t bytes)
{
    char buf[64];
    if (bytes < 1024 * 1024) std::snprintf(buf, sizeof(buf), "%llu KB", (unsigned long long)(bytes / 1024));
    else std::snprintf(buf, sizeof(buf), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return buf;
}

// Minimal wx dialog: a checkbox per candidate, an Import button, a Skip button.
class ImportDialog : public wxDialog
{
public:
    ImportDialog(wxWindow *parent, const std::vector<ImportCandidate> &candidates)
        : wxDialog(parent, wxID_ANY, _L("Import settings from another slicer"),
                   wxDefaultPosition, wxSize(560, -1), wxDEFAULT_DIALOG_STYLE)
        , m_candidates(candidates)
    {
        auto *root = new wxBoxSizer(wxVERTICAL);
        auto *intro = new wxStaticText(this, wxID_ANY,
            _L("We found config directories from other slicers. Select any you'd like to import — "
               "your printers, filaments and print profiles will be copied into this fork. "
               "Nothing is deleted from the source."));
        intro->Wrap(520);
        root->Add(intro, 0, wxALL | wxEXPAND, 14);

        for (size_t i = 0; i < m_candidates.size(); ++i) {
            const auto &c = m_candidates[i];
            wxString line = wxString::FromUTF8(
                c.label + "  —  " + human_size(c.size_bytes) +
                (c.printer_profiles > 0 ? ", " + std::to_string(c.printer_profiles) + " printer profiles" : ""));
            if (!c.supported_now) line += " " + _L("(not yet supported)");
            auto *cb = new wxCheckBox(this, wxID_ANY, line);
            cb->SetValue(c.supported_now && i == 0);
            cb->Enable(c.supported_now);
            m_boxes.push_back(cb);
            root->Add(cb, 0, wxLEFT | wxRIGHT | wxBOTTOM, 14);
            auto *path = new wxStaticText(this, wxID_ANY,
                wxString::FromUTF8(c.path.generic_string()));
            path->SetForegroundColour(wxColour(110, 110, 110));
            root->Add(path, 0, wxLEFT | wxRIGHT | wxBOTTOM, 28);
        }

        auto *btns = new wxBoxSizer(wxHORIZONTAL);
        btns->AddStretchSpacer(1);
        auto *skip  = new wxButton(this, wxID_CANCEL, _L("Skip"));
        auto *imp   = new wxButton(this, wxID_OK,     _L("Import selected"));
        imp->SetDefault();
        btns->Add(skip, 0, wxRIGHT, 8);
        btns->Add(imp,  0, 0, 0);
        root->Add(btns, 0, wxALL | wxEXPAND, 14);
        SetSizerAndFit(root);
    }

    std::vector<ImportCandidate> selected() const
    {
        std::vector<ImportCandidate> out;
        for (size_t i = 0; i < m_candidates.size(); ++i)
            if (m_boxes[i]->IsChecked() && m_candidates[i].supported_now)
                out.push_back(m_candidates[i]);
        return out;
    }

private:
    std::vector<ImportCandidate> m_candidates;
    std::vector<wxCheckBox *>    m_boxes;
};

// ---- Run ------------------------------------------------------------------

static bool perform_imports(const std::vector<ImportCandidate> &picked,
                            std::string                        &summary_out)
{
    const fs::path dst = fs::path(Slic3r::data_dir());
    int ok = 0;
    std::ostringstream summary;
    for (const auto &c : picked) {
        if (!c.supported_now) continue;
        if (c.source == ImportSource::OrcaSlicer || c.source == ImportSource::BambuStudio) {
            std::string err;
            if (copy_config_tree(c.path, dst, &err)) {
                ++ok;
                summary << c.label << ": directory copied.\n";
            } else {
                BOOST_LOG_TRIVIAL(error) << "config-import " << c.label << ": " << err;
            }
        } else if (c.source == ImportSource::PrusaSlicer) {
            PrusaImportStats s = import_prusa(c.path, dst);
            if (s.files_written > 0) ++ok;
            summary << c.label << ": wrote " << s.files_written
                    << " profiles, translated " << s.keys_translated << " keys, "
                    << s.keys_unmapped << " unmapped (see config-import-prusa.log).\n";
        }
    }
    summary_out = summary.str();
    return ok > 0;
}

bool run_import_dialog(const std::vector<ImportCandidate> &candidates)
{
    if (candidates.empty()) return false;
    ImportDialog dlg(nullptr, candidates);
    if (dlg.ShowModal() != wxID_OK) return false;
    auto picked = dlg.selected();
    if (picked.empty()) return false;

    std::string summary;
    bool ok = perform_imports(picked, summary);
    if (ok) {
        wxString msg = _L("Import complete. Restart the slicer to see the imported profiles.");
        if (!summary.empty())
            msg += "\n\n" + wxString::FromUTF8(summary);
        wxMessageBox(msg, _L("Import"), wxOK | wxICON_INFORMATION);
    } else {
        wxMessageBox(_L("Nothing was imported — see the log for details."),
                     _L("Import"), wxOK | wxICON_WARNING);
    }
    return ok;
}

bool run_import_from_menu()
{
    auto candidates = detect_import_candidates();
    if (candidates.empty()) {
        wxMessageBox(_L("No other slicer config directories found on this machine."),
                     _L("Import"), wxOK | wxICON_INFORMATION);
        return false;
    }
    return run_import_dialog(candidates);
}

}} // namespace Slic3r::GUI
