#pragma once

#include "editor_panel.h"
#include "Honey/core/console_buffer.h"

#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace Honey {

    class ConsolePanel : public EditorPanel {
    public:
        const char* name() const override { return "Console"; }
        void on_imgui_render(EditorLayer& editor) override;

    private:
        static int input_callback(ImGuiInputTextCallbackData* data);

        void pull_new_entries();
        void rebuild_visible();
        void clear_selection() { m_sel_anchor = m_sel_cursor = -1; m_selecting = false; }
        std::string selected_text() const;
        void submit_command(const std::string& command);
        std::string format_entry(const ConsoleEntry& entry) const;
        bool passes_filter(const ConsoleEntry& entry) const;

        std::vector<ConsoleEntry> m_entries;
        std::vector<int> m_visible; // indices into m_entries that pass the level + text filters
        uint64_t m_last_seq = 0;
        bool m_dirty = true;

        bool m_show_level[4] = {true, true, true, true}; // trace/debug, info, warn, error/critical
        bool m_show_time = true;
        bool m_show_function = true;
        char m_filter[128] = "";

        // Row-granular selection over m_visible (inclusive range between anchor and cursor, -1 = none).
        int m_sel_anchor = -1;
        int m_sel_cursor = -1;
        bool m_selecting = false;

        char m_input[512] = "";
        std::vector<std::string> m_history;
        int m_history_pos = -1;
        bool m_refocus_input = false;
    };

}
