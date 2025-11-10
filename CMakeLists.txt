cmake_minimum_required(VERSION 3.31)
project(Honey_Editor)

set(CMAKE_CXX_STANDARD 20)

add_executable(Honey_Editor src/honey_editor_app.cpp
        src/editor_layer.cpp
        src/editor_layer.h
        src/panels/scene_hierarchy_panel.h
        src/panels/scene_hierarchy_panel.cpp
        assets/scripts/camera_controller.h
        src/panels/content_browser_panel.h
        src/panels/content_browser_panel.cpp
        assets/scripts/player_controller.h
        assets/scripts/script_registrar.h)

target_include_directories(Honey_Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/Honey/engine/src
)
target_include_directories(Honey_Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/Honey/engine/vendor
)

add_compile_definitions(GLM_FORCE_SILENT_WARNINGS)

add_subdirectory(Honey)
target_link_libraries(Honey_Editor PRIVATE engine)

add_compile_definitions(
        ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets"
)


