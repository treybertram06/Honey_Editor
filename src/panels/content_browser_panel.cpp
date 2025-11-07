#include "hnpch.h"
#include "content_browser_panel.h"

#include <imgui.h>
#include <filesystem>

namespace Honey {

    //will change when theres separate projects
    const static std::filesystem::path s_assests_dir = "../assets/";

    ContentBrowserPanel::ContentBrowserPanel()
        : m_current_directory(s_assests_dir) {}

    void ContentBrowserPanel::on_imgui_render() {
        ImGui::Begin("Content Browser");

        if (m_current_directory != s_assests_dir) {
            if (ImGui::Button("<-")) {
                m_current_directory = m_current_directory.parent_path();
            }
        }

        for (auto& it : std::filesystem::directory_iterator(m_current_directory)) {
            const auto&  path = it.path();
            auto relative_path = std::filesystem::relative(it.path(), m_current_directory);

            if (it.is_directory()) {
                if (ImGui::Button(path.c_str())) {
                    m_current_directory /= it.path().filename();
                }
            } else {
                if (ImGui::Button(path.c_str())) {

                }
            }

        }

        ImGui::End();
    }
}
