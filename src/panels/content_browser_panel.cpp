#include "hnpch.h"
#include "content_browser_panel.h"

#include <imgui.h>
#include <filesystem>

#include "../../Honey/engine/src/Honey/ui/imgui_utils.h"

namespace Honey {

    //will change when theres separate projects
    extern const std::filesystem::path g_assets_dir = "../assets";

    ContentBrowserPanel::ContentBrowserPanel()
        : m_current_directory(g_assets_dir) {
        m_directory_icon = Texture2D::create("../resources/icons/content_browser/folder.png");
        m_file_icon = Texture2D::create("../resources/icons/content_browser/document.png");
    }

    void ContentBrowserPanel::on_imgui_render() {
        ImGui::Begin("Content Browser");

        if (m_current_directory != std::filesystem::path(g_assets_dir)) {
            if (ImGui::Button("<-")) {
                m_current_directory = m_current_directory.parent_path();
            }
        }

        static float padding = 16.0f;
        static float thumbnail_size = 80.0f;
        float cell_size = thumbnail_size + padding;

        float panel_width = ImGui::GetContentRegionAvail().x;
        int column_count = (int)(panel_width / cell_size);
        if (column_count < 1) column_count = 1;

        ImGui::Columns(column_count, "content_browser_columns", false);

        for (auto& it : std::filesystem::directory_iterator(m_current_directory)) {
            const auto& path = it.path();
            auto relative_path = std::filesystem::relative(path, g_assets_dir);
            std::string filename_string = relative_path.filename().string();

            Ref<Texture2D> icon = get_icon_for_extension(path.string());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            UI::ImageButton(filename_string.c_str(), icon->get_imgui_texture_id(), { thumbnail_size, thumbnail_size });

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                std::string path_str = relative_path.string();
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM",
                    path_str.c_str(), path_str.size() + 1, ImGuiCond_Once);
                ImGui::TextUnformatted(path_str.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (it.is_directory()) {
                    m_current_directory /= path.filename();
                }
            }
            ImGui::TextWrapped(filename_string.c_str());

            ImGui::NextColumn();

        }

        ImGui::Columns(1);

        //ImGui::SliderFloat("Thumbnail Size", &thumbnail_size, 16.0f, 512.0f);
        //ImGui::SliderFloat("Padding", &padding, 0.0f, 64.0f);

        ImGui::End();
    }

    Ref<Texture2D> ContentBrowserPanel::get_icon_for_extension(const std::string& path) {
        if (std::filesystem::is_directory(path)) {
            return m_directory_icon;
        }
        return m_file_icon;
    }
}
