#pragma once
#include "Honey/renderer/texture.h"
#include <filesystem>

namespace Honey {
    class ContentBrowserPanel {
    public:
        ContentBrowserPanel();
        ~ContentBrowserPanel() = default;

        void on_imgui_render();

    private:
        Ref<Texture2D> get_icon_for_extension(const std::string& path);

        enum class ViewMode {
            Grid = 0,
            List = 1
        };

        std::filesystem::path m_current_directory = "/";
        std::filesystem::path m_selected_path;   // currently selected item (for highlight / context menu)

        Ref<Texture2D> m_directory_icon;
        Ref<Texture2D> m_file_icon;

        char m_search_filter[128] = "";
        bool m_show_hidden = true;

        ViewMode m_view_mode = ViewMode::Grid;   // default to grid view
    };
}