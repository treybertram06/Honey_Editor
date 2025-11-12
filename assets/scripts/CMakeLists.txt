cmake_minimum_required(VERSION 3.20)
project(HoneyScripts)

set(CMAKE_CXX_STANDARD 20)

# Collect all script files automatically
file(GLOB_RECURSE SCRIPT_SOURCES "*.cpp" "*.h")

# Debug: print all found script files
message(STATUS "Found script sources:")
foreach(SRC ${SCRIPT_SOURCES})
    message(STATUS "  ${SRC}")
endforeach()

# Build as a shared library
add_library(HoneyScripts SHARED ${SCRIPT_SOURCES}
        script_entry.cpp)

# Force inclusion of all object files to ensure static initializers run
if (MSVC)
    target_link_options(HoneyScripts PRIVATE /WHOLEARCHIVE)
else()
    target_link_options(HoneyScripts PRIVATE -Wl,--whole-archive -Wl,--no-whole-archive)
endif()

# Include engine headers
target_include_directories(HoneyScripts PRIVATE
    ${CMAKE_SOURCE_DIR}/Honey/engine/src
)

# Link against the engine (for ScriptableEntity, etc.)
target_link_libraries(HoneyScripts PRIVATE engine)

# Allow engine symbols to be visible to the script library
set_target_properties(HoneyScripts PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/scripts"
)

# Enable symbol exports for cross-platform dynamic linking
target_compile_definitions(HoneyScripts PRIVATE HONEY_SCRIPTS_EXPORTS)
