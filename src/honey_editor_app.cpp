#include <Honey.h>
#include <Honey/core/entry_point.h>

#include "editor_layer.h"

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

    AssetPaths::Mode asset_paths_mode() { return AssetPaths::Mode::editor; }

    Application* create_application() {
        return new HoneyEditor();
    }
}
