#pragma once
#include "Honey/renderer/texture.h"

namespace Honey {
    class ContentBrowserPanel {
        public:
            ContentBrowserPanel();
            ~ContentBrowserPanel() = default;

            void on_imgui_render();

    private:

        Ref<Texture2D> get_icon_for_extension(const std::string& path);

        std::filesystem::path m_current_directory = "/";

        Ref<Texture2D> m_directory_icon;
        Ref<Texture2D> m_file_icon;
    };
}
