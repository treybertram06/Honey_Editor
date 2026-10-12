#pragma once
#include "Honey/core/base.h"
#include "editor_panel.h"
#include "Honey/scene/scene.h"
#include "Honey/scene/components.h"
#include "Honey/scene/entity.h"


#include "Honey/ui/notifications.h"


namespace Honey {

    class SceneHierarchyPanel : public EditorPanel {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(const Ref<Scene>& context);

        void set_context(const Ref<Scene>& context);

        const char* name() const override { return "Scene Hierarchy"; }
        void on_imgui_render(EditorLayer& editor) override;

        // Draws the selected entity's components (hosted by PropertiesPanel).
        void draw_properties();

        Entity get_selected_entity() const { return m_selected_entity; }
        void set_selected_entity(Entity entity) { m_selected_entity = entity; }

        void set_notification_center(UI::NotificationCenter* nc) { m_notification_center = nc; }

        void delete_entity(Entity entity);
    private:
        bool is_descendant(Entity entity, Entity ancestor) const;
        void populate_selection_parent_set(Entity child);

        Ref<Scene> m_context;
        Entity m_selected_entity;
        std::vector<Entity> m_selection_parent_set;
        UI::NotificationCenter* m_notification_center = nullptr;

        void draw_entity_node(Entity entity);
        void draw_components(Entity entity);
    };

    // Separate dockable window showing the hierarchy panel's current selection.
    class PropertiesPanel : public EditorPanel {
    public:
        explicit PropertiesPanel(SceneHierarchyPanel& hierarchy) : m_hierarchy(hierarchy) {}

        const char* name() const override { return "Properties"; }
        void on_imgui_render(EditorLayer& editor) override;

    private:
        SceneHierarchyPanel& m_hierarchy;
    };
}
