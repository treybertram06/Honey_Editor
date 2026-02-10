#pragma once
#include "Honey/core/base.h"
#include "Honey/scene/scene.h"
#include "Honey/scene/components.h"
#include "Honey/scene/entity.h"


#include "Honey/ui/notifications.h"


namespace Honey {

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(const Ref<Scene>& context);

        void set_context(const Ref<Scene>& context);

        void on_imgui_render();

        Entity get_selected_entity() const { return m_selected_entity; }
        void set_selected_entity(Entity entity) { m_selected_entity = entity; }

        void set_notification_center(UI::NotificationCenter* nc) { m_notification_center = nc; }
    private:
        bool is_descendant(Entity entity, Entity ancestor) const;

        Ref<Scene> m_context;
        Entity m_selected_entity;
        UI::NotificationCenter* m_notification_center = nullptr;

        void draw_entity_node(Entity entity);
        void draw_components(Entity entity);
    };
}
