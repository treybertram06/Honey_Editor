cmake_minimum_required(VERSION 3.31)
project(Honey_Editor)

set(CMAKE_CXX_STANDARD 20)

add_executable(Honey_Editor src/honey_editor_app.cpp
        src/editor_layer.cpp
        src/editor_layer.h
        src/panels/scene_hierarchy_panel.h
        src/panels/scene_hierarchy_panel.cpp
        src/panels/content_browser_panel.h
        src/panels/content_browser_panel.cpp
        src/scripting/script_loader.h
        src/scripting/script_loader.cpp
)

# Enable symbol exports for dynamic linking (needed for script library)
set_target_properties(Honey_Editor PROPERTIES ENABLE_EXPORTS ON)

# Link against dl only on Linux
if (UNIX AND NOT APPLE)
    target_link_libraries(Honey_Editor PRIVATE dl)
endif()

target_include_directories(Honey_Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/Honey/engine/src
)
target_include_directories(Honey_Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/Honey/engine/vendor
)

add_compile_definitions(GLM_FORCE_SILENT_WARNINGS)

add_subdirectory(Honey)
add_subdirectory(assets/scripts)
target_link_libraries(Honey_Editor PRIVATE engine)

add_compile_definitions(
        ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets"
)
