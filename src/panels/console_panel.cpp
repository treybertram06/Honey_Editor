#include "hnpch.h"
#include "console_panel.h"
#include "Honey/core/console_commands.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace Honey {

    namespace {
        constexpr size_t max_history = 100;

        int level_bucket(spdlog::level::level_enum level) {
            if (level >= spdlog::level::err)  return 3;
            if (level == spdlog::level::warn) return 2;
            if (level == spdlog::level::info) return 1;
            return 0;
        }

        ImVec4 entry_color(const ConsoleEntry& entry) {
            if (entry.is_command) return {0.4f, 0.85f, 0.95f, 1.0f};
            switch (level_bucket(entry.level)) {
                case 3:  return {1.0f, 0.35f, 0.35f, 1.0f};
                case 2:  return {1.0f, 0.85f, 0.3f, 1.0f};
                case 1:  return {0.82f, 0.82f, 0.82f, 1.0f};
                default: return {0.5f, 0.5f, 0.5f, 1.0f};
            }
        }

        std::string format_time(std::chrono::system_clock::time_point tp) {
            const std::time_t t = std::chrono::system_clock::to_time_t(tp);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[16];
            std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
            return buf;
        }
    }

    std::string ConsolePanel::format_entry(const ConsoleEntry& entry) const {
        std::string line;
        if (m_show_time)
            line += "[" + format_time(entry.time) + "] ";
        if (!entry.is_command && !entry.logger.empty())
            line += entry.logger + ": ";
        if (m_show_function && !entry.function.empty())
            line += "[" + entry.function + "] ";
        line += entry.text;
        return line;
    }

    std::string ConsolePanel::selected_text() const {
        if (m_sel_anchor < 0 || m_visible.empty())
            return {};
        const int lo = std::max(0, std::min(m_sel_anchor, m_sel_cursor));
        const int hi = std::min((int)m_visible.size() - 1, std::max(m_sel_anchor, m_sel_cursor));
        std::string text;
        for (int row = lo; row <= hi; ++row)
            text += format_entry(m_entries[m_visible[row]]) + "\n";
        return text;
    }

    bool ConsolePanel::passes_filter(const ConsoleEntry& entry) const {
        if (!entry.is_command && !m_show_level[level_bucket(entry.level)])
            return false;
        if (m_filter[0] == '\0')
            return true;
        // Case-insensitive substring match.
        auto lower = [](char c) { return (char)std::tolower((unsigned char)c); };
        const std::string_view needle(m_filter);
        return std::search(entry.text.begin(), entry.text.end(), needle.begin(), needle.end(),
                           [&](char a, char b) { return lower(a) == lower(b); }) != entry.text.end();
    }

    void ConsolePanel::pull_new_entries() {
        const size_t before = m_entries.size();
        m_last_seq = ConsoleBuffer::copy_since(m_last_seq, m_entries);
        if (m_entries.size() == before)
            return;

        if (m_entries.size() > ConsoleBuffer::max_entries)
            m_entries.erase(m_entries.begin(), m_entries.end() - ConsoleBuffer::max_entries);
        m_dirty = true;
    }

    void ConsolePanel::rebuild_visible() {
        m_visible.clear();
        for (size_t i = 0; i < m_entries.size(); ++i)
            if (passes_filter(m_entries[i]))
                m_visible.push_back((int)i);
        m_dirty = false;
    }

    void ConsolePanel::submit_command(const std::string& command) {
        if (command.empty())
            return;

        if (m_history.empty() || m_history.back() != command) {
            m_history.push_back(command);
            if (m_history.size() > max_history)
                m_history.erase(m_history.begin());
        }

        ConsoleEntry echo;
        echo.is_command = true;
        echo.text = "> " + command;
        echo.time = std::chrono::system_clock::now();
        ConsoleBuffer::push(std::move(echo));

        ConsoleCommands::execute(command);
    }

    int ConsolePanel::input_callback(ImGuiInputTextCallbackData* data) {
        auto* panel = static_cast<ConsolePanel*>(data->UserData);
        if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || panel->m_history.empty())
            return 0;

        const int last = (int)panel->m_history.size() - 1;
        if (data->EventKey == ImGuiKey_UpArrow)
            panel->m_history_pos = panel->m_history_pos < 0 ? last : std::max(0, panel->m_history_pos - 1);
        else if (data->EventKey == ImGuiKey_DownArrow)
            panel->m_history_pos = panel->m_history_pos < 0 || panel->m_history_pos >= last ? -1 : panel->m_history_pos + 1;

        const std::string& text = panel->m_history_pos < 0 ? std::string() : panel->m_history[panel->m_history_pos];
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, text.c_str());
        return 0;
    }

    void ConsolePanel::on_imgui_render(EditorLayer&) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.047f, 0.047f, 0.047f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.047f, 0.047f, 0.047f, 1.0f));
        const bool visible = ImGui::Begin(name(), &m_open);
        if (!visible) {
            ImGui::End();
            ImGui::PopStyleColor(2);
            return;
        }

        const uint64_t seq_before = m_last_seq;
        pull_new_entries();
        const bool got_new = m_last_seq != seq_before;

        // Toolbar
        if (ImGui::Button("Clear")) {
            ConsoleBuffer::clear();
            m_entries.clear();
            m_dirty = true;
            clear_selection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            if (m_dirty) rebuild_visible();
            std::string text = selected_text();
            if (text.empty())
                for (int idx : m_visible)
                    text += format_entry(m_entries[idx]) + "\n";
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        static const char* level_labels[4] = {"T", "I", "W", "E"};
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Checkbox(level_labels[i], &m_show_level[i])) {
                m_dirty = true;
                clear_selection();
            }
            ImGui::SameLine();
        }
        ImGui::Checkbox("Time", &m_show_time);
        ImGui::SameLine();
        ImGui::Checkbox("Func", &m_show_function);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##console_filter", "Filter...", m_filter, sizeof(m_filter))) {
            m_dirty = true;
            clear_selection();
        }

        if (m_dirty)
            rebuild_visible();

#ifndef BUILD_DEBUG
        ImGui::TextDisabled("Release build: engine core logs are compiled out; only app and notification logs appear.");
#endif

        // Log region
        const float input_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("##console_scroll", ImVec2(0, -input_height), false, ImGuiWindowFlags_HorizontalScrollbar);

        const bool was_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

        // Selection: click/drag to select rows, Shift+click to extend, Ctrl/Cmd+C or right-click to copy.
        const float row_height = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 content_top = ImGui::GetCursorScreenPos(); // already scroll-adjusted
        const ImVec2 win_pos = ImGui::GetWindowPos();
        const ImVec2 win_size = ImGui::GetWindowSize();
        const int row_count = (int)m_visible.size();
        const ImVec2 mouse = ImGui::GetMousePos();
        const int mouse_row = (int)std::floor((mouse.y - content_top.y) / row_height);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (mouse_row >= 0 && mouse_row < row_count) {
                if (ImGui::GetIO().KeyShift && m_sel_anchor >= 0) {
                    m_sel_cursor = mouse_row;
                } else {
                    m_sel_anchor = m_sel_cursor = mouse_row;
                }
                m_selecting = true;
            } else {
                clear_selection();
            }
        }
        if (m_selecting) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_sel_cursor = std::clamp(mouse_row, 0, std::max(0, row_count - 1));
                // Scroll while dragging past the top/bottom edge.
                if (mouse.y < win_pos.y)
                    ImGui::SetScrollY(ImGui::GetScrollY() - row_height * 0.5f);
                else if (mouse.y > win_pos.y + win_size.y)
                    ImGui::SetScrollY(ImGui::GetScrollY() + row_height * 0.5f);
            } else {
                m_selecting = false;
            }
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const bool in_selection = m_sel_anchor >= 0 && mouse_row >= std::min(m_sel_anchor, m_sel_cursor) &&
                                      mouse_row <= std::max(m_sel_anchor, m_sel_cursor);
            if (!in_selection && mouse_row >= 0 && mouse_row < row_count)
                m_sel_anchor = m_sel_cursor = mouse_row;
            ImGui::OpenPopup("##console_ctx");
        }

        const int sel_lo = m_sel_anchor < 0 ? -1 : std::min(m_sel_anchor, m_sel_cursor);
        const int sel_hi = m_sel_anchor < 0 ? -1 : std::max(m_sel_anchor, m_sel_cursor);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGuiListClipper clipper;
        clipper.Begin(row_count, row_height);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const ConsoleEntry& entry = m_entries[m_visible[row]];
                if (row >= sel_lo && row <= sel_hi) {
                    const float y = content_top.y + row * row_height;
                    draw->AddRectFilled({win_pos.x, y}, {win_pos.x + win_size.x, y + row_height},
                                        IM_COL32(66, 115, 190, 110));
                }
                ImGui::PushStyleColor(ImGuiCol_Text, entry_color(entry));
                const std::string line = format_entry(entry);
                ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
                ImGui::PopStyleColor();
            }
        }
        clipper.End();

        // Ctrl/Cmd+C and Ctrl/Cmd+A while the log has focus (not while typing in the input line).
        if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_C, false) && m_sel_anchor >= 0)
                ImGui::SetClipboardText(selected_text().c_str());
            if (ImGui::IsKeyPressed(ImGuiKey_A, false) && row_count > 0) {
                m_sel_anchor = 0;
                m_sel_cursor = row_count - 1;
            }
        }

        if (ImGui::BeginPopup("##console_ctx")) {
            if (ImGui::MenuItem("Copy", nullptr, false, m_sel_anchor >= 0))
                ImGui::SetClipboardText(selected_text().c_str());
            if (ImGui::MenuItem("Select all", nullptr, false, row_count > 0)) {
                m_sel_anchor = 0;
                m_sel_cursor = row_count - 1;
            }
            ImGui::EndPopup();
        }

        // Follow new output only if the user hasn't scrolled away from the bottom.
        if (got_new && was_at_bottom && !m_selecting)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();

        // Input line
        ImGui::Separator();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (m_refocus_input) {
            ImGui::SetKeyboardFocusHere();
            m_refocus_input = false;
        }
        constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
        if (ImGui::InputText("##console_input", m_input, sizeof(m_input), flags, &ConsolePanel::input_callback, this)) {
            submit_command(m_input);
            m_input[0] = '\0';
            m_history_pos = -1;
            m_refocus_input = true;
        }

        ImGui::End();
        ImGui::PopStyleColor(2);
    }

}
