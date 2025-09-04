#pragma once
#include "Honey/core/base.h"
#include "Honey/scene/scene.h"
#include "Honey/scene/components.h"
#include "Honey/scene/entity.h"


namespace Honey {

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(const Ref<Scene>& context);

        void set_context(const Ref<Scene>& context);

        void on_imgui_render();

        Entity get_selected_entity() const { return m_selected_entity; }
        void set_selected_entity(Entity entity) { m_selected_entity = entity; }
    private:
        Ref<Scene> m_context;
        Entity m_selected_entity;

        void draw_entity_node(Entity entity);
        void draw_components(Entity entity);
    };
}
