#include "hnpch.h"
#include "runtime_layer.h"

#include "Honey/scene/cloth_system.h"
#include "Honey/scene/scene_serializer.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {
    RuntimeLayer::RuntimeLayer()
        : Layer("RuntimeLayer") {}

    void RuntimeLayer::on_attach() {
        HN_PROFILE_FUNCTION();

        m_scene_viewport_renderer.initialize();
        const auto& rs = Settings::get().renderer;
        m_scene_viewport_renderer.set_settings(SceneViewportRenderSettings{
            .renderer_type = rs.renderer_type,
            .geometry_path = rs.geometry_path,
            .collect_frame_graph_timings = false,
            .log_frame_graph_pass_timings = false,
            .debug_pick_enabled = false,
        });

        // Actual window size, not the saved settings (which can be stale when maximized/fullscreen).
        // TODO(0.2): use framebuffer pixel size and follow resizes.
        const auto& window = Application::get().get_window();
        m_viewport_size = { (float)window.get_width(), (float)window.get_height() };
        m_scene_viewport_renderer.resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);

        const auto entry_path = asset_root / "scenes" / "main.hns";
        m_active_scene = open_scene(entry_path);
        if (!m_active_scene) {
            Application::quit(11);
            return;
        }
        m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);
        m_active_scene->on_runtime_start();
    }

    void RuntimeLayer::on_detach() {
        HN_PROFILE_FUNCTION();

        // Stop first so scripts' OnDestroy runs while the CLR and renderer are still alive.
        if (m_active_scene)
            m_active_scene->on_runtime_stop();

        if (auto* ctx = Application::get().get_window().get_context())
            ctx->wait_idle();

        m_scene_viewport_renderer.shutdown();
        m_active_scene = nullptr;
        HN_INFO("RuntimeLayer detached");
    }

    void RuntimeLayer::on_update(Timestep ts) {
        HN_PROFILE_FUNCTION();

        if (!m_active_scene)
            return;

        // TODO(0.2): Apply pending resize

        // Same as editor Play, so cloth behaves identically in both.
        if (m_active_scene->get_cloth_system())
            m_active_scene->get_cloth_system()->set_frame_dt(ts.get_seconds());

        bool paused = false; // Make useful later
        m_active_scene->on_update_runtime(ts, paused);

        SceneViewportRenderContext render_context{};
        if (fill_runtime_camera(render_context, *m_active_scene, ts))
            m_scene_viewport_renderer.render(render_context);
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
}
