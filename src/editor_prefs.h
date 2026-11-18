#pragma once

#include <string>
#include <vector>

namespace Honey {

    // Per-user editor state that is not an engine setting: which project was open last and the recent
    // projects list. Stored in user_data_dir()/editor_prefs.txt (never in a project, so projects stay
    // portable). Paths are .honeyproj files, absolute, most recent first.
    class EditorPrefs {
    public:
        static void load();

        static const std::string& last_project();               // empty = the last session had no project open
        static void set_last_project(const std::string& file);  // empty = closed
        static const std::vector<std::string>& recent_projects();
        static void add_recent(const std::string& file);
        static void remove_recent(const std::string& file);

    private:
        static void save();
    };

}
