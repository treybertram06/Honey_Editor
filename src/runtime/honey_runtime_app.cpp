#include <Honey.h>
#include <Honey/core/entry_point.h>

#include "runtime_layer.h"

namespace Honey {

    class HoneyRuntime : public Application {
    public:
        HoneyRuntime()
            : Application() {
            push_layer(new RuntimeLayer());
        }

        ~HoneyRuntime() {
        }
    };

    Application* create_application() {
        return new HoneyRuntime();
    }
}
