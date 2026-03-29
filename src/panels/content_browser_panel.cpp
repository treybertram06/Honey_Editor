#include "hnpch.h"
#include "content_browser_panel.h"

#include <imgui.h>
#include <filesystem>

#include "../../Honey/engine/src/Honey/ui/imgui_utils.h"

namespace Honey {

    static std::vector<std::string> split(const std::string& s, const std::string& delim) {
        std::vector<std::string> parts;
        size_t start = 0, pos;
        while ((pos = s.find(delim, start)) != std::string::npos) {
            parts.push_back(s.substr(start, pos - start));
            start = pos + delim.size();
        }
        parts.push_back(s.substr(start));
        return parts;
    }

    // Supports && (AND) and || (OR); || has lower precedence.
    // e.g. "foo && bar || baz" → (foo AND bar) OR baz
    static bool matches_filter(const std::string& haystack, const std::string& raw_filter) {
        std::string filter = raw_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        for (const auto& or_group : split(filter, "||")) {
            bool all_match = true;
            for (auto term : split(or_group, "&&")) {
                // trim whitespace
                auto l = term.find_first_not_of(' ');
                auto r = term.find_last_not_of(' ');
                term = (l == std::string::npos) ? "" : term.substr(l, r - l + 1);
                if (!term.empty() && haystack.find(term) == std::string::npos) {
                    all_match = false;
                    break;
                }
            }
            if (all_match) return true;
        }
        return false;
    }

    static bool directory_contains(const std::filesystem::path& dir, const std::string& raw_filter) {
        for (auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (matches_filter(name, raw_filter))
                return true;
        }
        return false;
    }

    //will change when theres separate projects
    extern const std::filesystem::path g_assets_dir = "../assets";

    ContentBrowserPanel::ContentBrowserPanel()
        : m_current_directory(g_assets_dir) {
        m_directory_icon = Texture2D::create_async("../resources/icons/content_browser/folder.png");
        m_file_icon = Texture2D::create_async("../resources/icons/content_browser/document.png");
    }

    void ContentBrowserPanel::on_imgui_render() {
        ImGui::Begin("Content Browser");

        // --- Top bar: breadcrumbs + view mode toggle ---
        {
            // Breadcrumbs: assets / foo / bar
            std::filesystem::path relative = std::filesystem::relative(m_current_directory, g_assets_dir);

            ImGui::TextUnformatted("Path: ");
            ImGui::SameLine();

            // Root "assets" segment
            if (ImGui::SmallButton("assets")) {
                m_current_directory = g_assets_dir;
                memset(m_search_filter, 0, sizeof(m_search_filter));
            }

            std::filesystem::path temp_path = g_assets_dir;
            for (const auto& part : relative) {
                ImGui::SameLine();
                ImGui::TextUnformatted("/");
                ImGui::SameLine();

                const std::string label = part.string();
                if (ImGui::SmallButton(label.c_str())) {
                    temp_path /= part;
                    m_current_directory = temp_path;
                    memset(m_search_filter, 0, sizeof(m_search_filter));
                } else {
                    temp_path /= part;
                }
            }

            ImGui::SameLine();
            ImGui::Dummy(ImVec2(8.0f, 0.0f));

            // View mode toggle
            ImGui::SameLine();
            ImGui::TextUnformatted("View:");
            ImGui::SameLine();

            bool grid_selected = (m_view_mode == ViewMode::Grid);
            bool list_selected = (m_view_mode == ViewMode::List);

            if (ImGui::RadioButton("Grid", grid_selected)) {
                m_view_mode = ViewMode::Grid;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("List", list_selected)) {
                m_view_mode = ViewMode::List;
            }
        }

        ImGui::Separator();

        // --- Search / options row ---
        ImGui::TextUnformatted("Search:");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##ContentSearch", m_search_filter, sizeof(m_search_filter),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            // filtering happens in the loop
                             }
        ImGui::SameLine();
        ImGui::Checkbox("Show Hidden", &m_show_hidden);
        ImGui::SameLine();
        ImGui::Checkbox("Recurse Search", &m_recurse_search);

        ImGui::Separator();

        static float padding = 16.0f;
        static float thumbnail_size = 80.0f;
        float cell_size = thumbnail_size + padding;

        float panel_width = ImGui::GetContentRegionAvail().x;
        int column_count = (int)(panel_width / cell_size);
        if (column_count < 1) column_count = 1;

        // --- List vs Grid layout setup ---
        if (m_view_mode == ViewMode::Grid) {
            ImGui::Columns(column_count, "content_browser_columns", false);
        } else {
            ImGui::Columns(1, "content_browser_columns", false);
        }

        for (auto& it : std::filesystem::directory_iterator(m_current_directory)) {
            const auto& path = it.path();
            auto relative_path = std::filesystem::relative(path, g_assets_dir);
            std::string filename_string = relative_path.filename().string();

            // Skip hidden files if option is off
            if (!m_show_hidden && !filename_string.empty() && filename_string[0] == '.')
                continue;

            // --- Filter by search query ---
            if (m_search_filter[0] != '\0') {
                std::string haystack = filename_string;
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);

                bool name_matches = matches_filter(haystack, m_search_filter);
                bool dir_matches  = false;
                if (m_recurse_search)
                    dir_matches = !name_matches && it.is_directory() && directory_contains(path, m_search_filter);

                if (!name_matches && !dir_matches)
                    continue; // Skip this entry if it doesn't match
            }

            Ref<Texture2D> icon = get_icon_for_extension(path.string());

            // Selection highlight
            bool is_selected = (m_selected_path == path);
            ImVec4 button_color = is_selected ? ImVec4(0.2f, 0.4f, 0.9f, 0.4f) : ImVec4(0, 0, 0, 0);
            ImGui::PushStyleColor(ImGuiCol_Button, button_color);

            // --- Draw icon (button) and optional text, depending on view mode ---
            if (m_view_mode == ViewMode::Grid) {
                // Grid: big square icon, label underneath
                if (UI::ImageButton(filename_string.c_str(),
                                    icon->get_imgui_texture_id(),
                                    { thumbnail_size, thumbnail_size })) {
                    m_selected_path = path;
                                    }
            } else {
                // List: smaller icon with text on the same row
                ImVec2 icon_size(thumbnail_size * 0.5f, thumbnail_size * 0.5f);
                if (UI::ImageButton(filename_string.c_str(),
                                    icon->get_imgui_texture_id(),
                                    icon_size)) {
                    m_selected_path = path;
                                    }

                // Attach context menu directly to this icon item
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Open")) {
                        if (it.is_directory()) {
                            m_current_directory /= path.filename();
                            memset(m_search_filter, 0, sizeof(m_search_filter));
                            m_selected_path.clear();
                        }
                        // else: future hook for opening files directly
                    }
                    ImGui::EndPopup();
                }

                ImGui::SameLine();
                ImGui::TextWrapped("%s", filename_string.c_str());
            }

            // Drag-drop source (attached to the icon item)
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                std::string path_str = relative_path.string();
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM",
                    path_str.c_str(), path_str.size() + 1, ImGuiCond_Once);
                ImGui::TextUnformatted(path_str.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopStyleColor();

            // Double-click to enter directory (works for both grid and list)
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (it.is_directory()) {
                    m_current_directory /= path.filename();
                    memset(m_search_filter, 0, sizeof(m_search_filter));
                    m_selected_path.clear();
                }
            }

            // Grid mode: attach context menu to the grid icon *after* it's drawn
            if (m_view_mode == ViewMode::Grid) {
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Open")) {
                        if (it.is_directory()) {
                            m_current_directory /= path.filename();
                            memset(m_search_filter, 0, sizeof(m_search_filter));
                            m_selected_path.clear();
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::TextWrapped("%s", filename_string.c_str());
            }

            ImGui::NextColumn();
        }

        ImGui::Columns(1);

        ImGui::End();
    }

    Ref<Texture2D> ContentBrowserPanel::get_icon_for_extension(const std::string& path) {
        if (std::filesystem::is_directory(path)) {
            return m_directory_icon;
        }
        return m_file_icon;
    }
}
