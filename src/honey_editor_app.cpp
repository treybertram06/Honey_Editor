#include <Honey.h>
#include <Honey/core/entry_point.h>

#include "editor_layer.h"

namespace Honey {
    class HoneyEditor : public Application {
    public:
        HoneyEditor()
            : Application("Honey Editor") {
            push_layer(new EditorLayer());
        }

        ~HoneyEditor() {
        }
    };

    Application* create_application() {
        return new HoneyEditor();
    }
}
