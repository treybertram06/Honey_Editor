#include "bird_controller.h"
#include "camera_controller.h"
#include "pipe_spawner.h"
#include "player_controller.h"
#include "test.h"
/*
// Dummy instances to force linker to include these translation units
namespace {
    void force_link_scripts() {
        // Create dummy objects to ensure the compiler generates code for these classes
        (void)sizeof(CameraController);
        (void)sizeof(PlayerController);
        (void)sizeof(Test);
        (void)sizeof(BirdController);
        (void)sizeof(PipeSpawner);
    }
}
*/
extern "C" void register_all_scripts() {
    using namespace Honey;
    auto& registry = ScriptRegistry::get();

    registry.register_script<CameraController>("CameraController");
    registry.register_script<PlayerController>("PlayerController");
    registry.register_script<Test>("Test");
    registry.register_script<BirdController>("BirdController");
    registry.register_script<PipeSpawner>("PipeSpawner");

    HN_CORE_INFO("All scripts registered successfully.");
}
