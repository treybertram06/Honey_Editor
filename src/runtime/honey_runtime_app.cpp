#include <Honey.h>
#include <Honey/core/entry_point.h>

#include "runtime_layer.h"
#include "Honey/core/game_manifest.h"

namespace Honey {

    class HoneyRuntime : public Application {
    public:
        HoneyRuntime()
            : Application() {
            set_persist_settings(false);
            push_layer(new RuntimeLayer());
        }

        ~HoneyRuntime() {
        }
    };

    Application* create_application() {
        if (!GameManifest::load(AssetPaths::manifest_path())) {
            HN_ERROR("Honey_Runtime requires a valid game manifest ({})", AssetPaths::manifest_path().string());
            return nullptr;
        }
        return new HoneyRuntime();
    }
}
