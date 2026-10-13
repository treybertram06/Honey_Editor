#include "hnpch.h"
#include "runtime_layer.h"

#include "Honey/scene/scene_serializer.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {
    RuntimeLayer::RuntimeLayer()
        : Layer("RuntimeLayer") {}

    void RuntimeLayer::on_attach() {
        // deserialize start scene (main.hns)
        m_active_scene = open_scene(asset_root / "scenes" / "main.hns");
        m_active_scene == nullptr ? HN_ERROR("Failed to load scene!") : HN_INFO("Loaded scene successfully");
        m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);
        m_active_scene->on_runtime_start();
    }

    void RuntimeLayer::on_detach() {
        m_active_scene->on_runtime_stop();
        m_active_scene = nullptr;
        HN_INFO("RuntimeLayer detached");
    }

    void RuntimeLayer::on_update(Timestep ts) {

    }

    Ref<Scene> RuntimeLayer::open_scene(const std::filesystem::path& path) {
        if (path.empty()) {
            HN_WARN("No scene path specified!");
            return nullptr;
        }
        if (path.extension() != ".hns") {
            HN_WARN("File {0} is not a Honey Scene file!", path.string());
            return nullptr;
        }

        Ref<Scene> new_scene = CreateRef<Scene>();
        SceneSerializer serializer(new_scene);
        const bool loaded = serializer.deserialize(path);

        if (!loaded) {
            HN_WARN("Failed to load scene {0}!", path.string());
            return nullptr;
        }

        return new_scene;
    }

    void RuntimeLayer::on_event(Event &event) {
        Layer::on_event(event);
    }

    bool RuntimeLayer::on_key_pressed(KeyPressedEvent &e) {
    }

    bool RuntimeLayer::on_mouse_button_pressed(MouseButtonPressedEvent &e) {
    }
}
