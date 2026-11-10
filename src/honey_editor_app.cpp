#include <Honey.h>
#include <Honey/core/entry_point.h>

#include "editor_layer.h"
#include "editor_paths.h"

namespace Honey {
    class HoneyEditor : public Application {
    public:
        HoneyEditor()
            : Application() {
            push_layer(new EditorLayer());
        }

        ~HoneyEditor() {
        }
    };

    Application* create_application() {
        const auto& manifest = AssetPaths::manifest_path();
        if (!std::filesystem::exists(manifest)) {
            std::error_code ec;
            std::filesystem::copy_file(EditorPaths::resource("defaults/game.yaml"), manifest, ec);
            if (ec)
                HN_ERROR("Could not create default game manifest at {}: {}", manifest.string(), ec.message());
            else
                HN_INFO("Created default game manifest at {}", manifest.string());
        }

        return new HoneyEditor();
    }
}
