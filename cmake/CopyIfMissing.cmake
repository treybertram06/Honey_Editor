# cmake/CopyIfMissing.cmake
if(NOT EXISTS "${DEST}")
    message(STATUS "imgui.ini not found — copying default")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${SRC}" "${DEST}")
endif()