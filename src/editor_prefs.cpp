#include "hnpch.h"
#include "editor_prefs.h"

#include "Honey/core/asset_paths.h"

#include <algorithm>
#include <fstream>

namespace Honey {

    namespace {
        constexpr size_t k_max_recent = 10;

        std::string s_last_project;
        std::vector<std::string> s_recent;

        std::filesystem::path prefs_path() { return AssetPaths::user_data_dir() / "editor_prefs.txt"; }
    }

    // Line format: "last=<path>" and "recent=<path>" (one per project). Unknown keys are ignored.
    void EditorPrefs::load() {
        s_last_project.clear();
        s_recent.clear();
        std::ifstream in(prefs_path());
        for (std::string line; std::getline(in, line);) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);
            if (value.empty())
                continue;
            if (key == "last")
                s_last_project = value;
            else if (key == "recent" && s_recent.size() < k_max_recent)
                s_recent.push_back(value);
        }
    }

    void EditorPrefs::save() {
        std::error_code ec;
        std::filesystem::create_directories(AssetPaths::user_data_dir(), ec);
        std::ofstream out(prefs_path(), std::ios::trunc);
        if (!out) {
            HN_WARN("Could not save editor prefs to {}", prefs_path().string());
            return;
        }
        out << "last=" << s_last_project << "\n";
        for (const auto& p : s_recent)
            out << "recent=" << p << "\n";
    }

    const std::string& EditorPrefs::last_project() { return s_last_project; }
    const std::vector<std::string>& EditorPrefs::recent_projects() { return s_recent; }

    void EditorPrefs::set_last_project(const std::string& file) {
        s_last_project = file;
        save();
    }

    void EditorPrefs::add_recent(const std::string& file) {
        std::erase(s_recent, file);
        s_recent.insert(s_recent.begin(), file);
        if (s_recent.size() > k_max_recent)
            s_recent.resize(k_max_recent);
        save();
    }

    void EditorPrefs::remove_recent(const std::string& file) {
        std::erase(s_recent, file);
        save();
    }

}
