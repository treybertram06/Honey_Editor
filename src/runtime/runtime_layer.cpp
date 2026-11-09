#include "hnpch.h"
#include "runtime_layer.h"

#include "Honey/core/asset_paths.h"
#include "Honey/scene/cloth_system.h"
#include "Honey/scene/scene_serializer.h"
#include "Honey/scripting/csharp_script_engine.h"

namespace Honey {
    RuntimeLayer::RuntimeLayer()
        : Layer("RuntimeLayer") {}

    void RuntimeLayer::on_attach() {
        HN_PROFILE_FUNCTION();

        if (!Application::get().get_vulkan_backend().supports_full_renderer()) {
            HN_ERROR("Honey_Runtime requires full renderer support."); // TODO(4.2)
            Application::quit(10);
            return;
        }

        m_scene_viewport_renderer.initialize();
        const auto& rs = Settings::get().renderer;
        m_scene_viewport_renderer.set_settings(SceneViewportRenderSettings{
            .renderer_type = rs.renderer_type == RendererSettings::RendererType::lite
            ? RendererSettings::RendererType::deferred : rs.renderer_type,
            .geometry_path = rs.geometry_path,
            .collect_frame_graph_timings = false,
            .log_frame_graph_pass_timings = false,
            .debug_pick_enabled = false,
        });

        // Actual framebuffer size, not the saved settings (which can be stale when maximized/fullscreen).
        const auto [fb_width, fb_height] = Application::get().get_window().get_framebuffer_size();
        m_viewport_size = { (float)fb_width, (float)fb_height };
        m_scene_viewport_renderer.resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);

        const auto entry_path = AssetPaths::resolve("scenes/main.hns"); // TODO(0.4): start scene from game.yaml
        m_active_scene = open_scene(entry_path);
        if (!m_active_scene) {
            Application::quit(11);
            return;
        }
        m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);
        CSharpScriptEngine::s_quit_requested = false; // drop any stale request
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

        m_scene_rendered_this_frame = false;
        if (!m_active_scene)
            return;

        if (m_pending_size.x > 0 && m_pending_size.y > 0 &&
            ((float)m_pending_size.x != m_viewport_size.x || (float)m_pending_size.y != m_viewport_size.y)) {
            m_viewport_size = { (float)m_pending_size.x, (float)m_pending_size.y };
            m_scene_viewport_renderer.resize(m_pending_size.x, m_pending_size.y);
            m_active_scene->on_viewport_resize(m_pending_size.x, m_pending_size.y);
        }

        // Same as editor Play, so cloth behaves identically in both.
        if (m_active_scene->get_cloth_system())
            m_active_scene->get_cloth_system()->set_frame_dt(ts.get_seconds());

        bool paused = false; // Make useful later
        m_active_scene->on_update_runtime(ts, paused);

        // Application.Quit() from a script only sets a flag; act on it here, between frames,
        // not inside the script callback. on_detach runs on_runtime_stop during shutdown.
        if (CSharpScriptEngine::s_quit_requested) {
            CSharpScriptEngine::s_quit_requested = false;
            Application::quit();
            return;
        }

        SceneViewportRenderContext render_context{};
        if (fill_runtime_camera(render_context, *m_active_scene, ts)) {
            m_scene_viewport_renderer.render(render_context);
            m_scene_rendered_this_frame = true;
        }
    }

    void RuntimeLayer::on_window_render() {
        HN_PROFILE_FUNCTION();

        if (m_scene_rendered_this_frame)
            Renderer::present(m_scene_viewport_renderer.get_output_framebuffer(), PresentFit::letterbox);
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
        EventDispatcher dispatcher(event);
        dispatcher.dispatch<FramebufferResizeEvent>([this](FramebufferResizeEvent& e) {
            m_pending_size = { e.get_width(), e.get_height() };
            return false;
        });
    }
}
