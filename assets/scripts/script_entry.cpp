#include "camera_controller.h"
#include "player_controller.h"
#include "test.h"

// Dummy instances to force linker to include these translation units
namespace {
    void force_link_scripts() {
        // Create dummy objects to ensure the compiler generates code for these classes
        (void)sizeof(CameraController);
        (void)sizeof(PlayerController);
        (void)sizeof(Test);
    }
}

