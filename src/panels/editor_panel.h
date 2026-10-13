#pragma once

namespace Honey {
    class EditorLayer;

    // Thin base for dockable editor windows. EditorLayer renders every registered panel while open()
    // is true and builds the View menu toggles from name().
    class EditorPanel {
    public:
        virtual ~EditorPanel() = default;

        virtual const char* name() const = 0;
        virtual void on_imgui_render(EditorLayer& editor) = 0;

        bool& open() { return m_open; }

    protected:
        bool m_open = true;
    };
}
