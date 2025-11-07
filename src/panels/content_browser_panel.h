#pragma once

namespace Honey {
    class ContentBrowserPanel {
        public:
            ContentBrowserPanel();
            ~ContentBrowserPanel() = default;

            void on_imgui_render();

    private:
        std::filesystem::path m_current_directory = "/";
    };
}