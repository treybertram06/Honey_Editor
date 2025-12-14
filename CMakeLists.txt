cmake_minimum_required(VERSION 3.16)
project(Honey_Editor)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Honey_Editor
        src/honey_editor_app.cpp
        src/editor_layer.cpp
        src/editor_layer.h
        src/panels/scene_hierarchy_panel.h
        src/panels/scene_hierarchy_panel.cpp
        src/panels/content_browser_panel.h
        src/panels/content_browser_panel.cpp
        src/scripting/script_loader.h
        src/scripting/script_loader.cpp
)

# Needed for script DLLs
set_target_properties(Honey_Editor PROPERTIES ENABLE_EXPORTS ON)

# Linux dlopen
if(UNIX AND NOT APPLE)
    target_link_libraries(Honey_Editor PRIVATE dl)
endif()

target_include_directories(Honey_Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/Honey/engine/src
        ${CMAKE_SOURCE_DIR}/Honey/engine/vendor
)

add_compile_definitions(
        GLM_FORCE_SILENT_WARNINGS
        ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets"
)

# ---- Engine ----
add_subdirectory(Honey)
target_link_libraries(Honey_Editor PRIVATE engine)

# ==========================================================
# ==================== MONO (WINDOWS) ======================
# ==========================================================
if(WIN32)
    set(MONO_ROOT "${CMAKE_SOURCE_DIR}/Honey/engine/vendor/mono-bin/windows/x64")

    # Link Mono AGAIN here (static libs do not propagate)
    find_library(MONO_LIB
            NAMES
            monosgen-2.0
            mono-2.0-sgen
            mono-2.0
            PATHS
            "${MONO_ROOT}/lib"
            "${MONO_ROOT}/bin"
            REQUIRED
    )

    target_link_libraries(Honey_Editor PRIVATE "${MONO_LIB}")

    # Ensure DLL is next to exe
    set(MONO_DLL
            "${MONO_ROOT}/bin/mono-2.0-sgen.dll"
    )

    if(NOT EXISTS "${MONO_DLL}")
        message(FATAL_ERROR "Mono DLL not found: ${MONO_DLL}")
    endif()

    add_custom_command(TARGET Honey_Editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MONO_DLL}"
            "$<TARGET_FILE_DIR:Honey_Editor>"
    )
endif()


# Move shaderc dll next to executable
add_custom_command(TARGET Honey_Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:Honey_Editor>
        $<TARGET_FILE_DIR:Honey_Editor>
        COMMAND_EXPAND_LISTS
)
