cmake_minimum_required(VERSION 3.11)
project(easy_ffmpeg-tests)

# ---Create executable---
add_executable(${PROJECT_NAME} tests.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

# ---Output to bin/ so that we can easily grab only the exe and the dyn libs---
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../bin)

# ---Set warning level---
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -pedantic-errors -Wconversion -Wsign-conversion -Wimplicit-fallthrough)
endif()

# ---Maybe enable warnings as errors---
if(WARNINGS_AS_ERRORS_FOR_EASY_FFMPEG)
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /WX)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
    endif()
endif()

# ---Include our library---
add_subdirectory(.. ${CMAKE_CURRENT_SOURCE_DIR}/build/easy_ffmpeg)
target_link_libraries(${PROJECT_NAME} PRIVATE easy_ffmpeg::easy_ffmpeg)
ffmpeg_copy_libs(${PROJECT_NAME})

# Copy test gif
include("../CMakeUtils/files_and_folders.cmake")
Cool__target_copy_file(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/test.gif")
Cool__target_copy_file(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/expected_frame_0.txt")
Cool__target_copy_file(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/expected_frame_3.txt")

# ---Add doctest---
include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest
    GIT_TAG b7c21ec5ceeadb4951b00396fc1e4642dd347e5f
)
FetchContent_MakeAvailable(doctest)
target_link_libraries(${PROJECT_NAME} PRIVATE doctest::doctest)

# ---Add quick_imgui---
include(FetchContent)
FetchContent_Declare(
    quick_imgui
    GIT_REPOSITORY https://github.com/CoolLibs/quick_imgui
    GIT_TAG abadef68ea1e9d7b607b7d57566a1f1ac95ec4fa
)
FetchContent_MakeAvailable(quick_imgui)
target_include_directories(easy_ffmpeg SYSTEM PRIVATE ${quick_imgui_SOURCE_DIR}/lib) # Give our library access to Dear ImGui
target_link_libraries(${PROJECT_NAME} PRIVATE quick_imgui::quick_imgui)

# ---Add exe_path---
FetchContent_Declare(
    exe_path
    GIT_REPOSITORY https://github.com/CoolLibs/exe_path
    GIT_TAG 198dd0c2a077ffaed5ecbe9f45b9836e8b3c9fc7
)
FetchContent_MakeAvailable(exe_path)
target_link_libraries(${PROJECT_NAME} PRIVATE exe_path::exe_path)

# ---Ignore .vscode/settings.json in Git---
find_package(Git QUIET)

if(GIT_FOUND)
    get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

    if(EXISTS "${PARENT_DIR}/.git")
        execute_process(COMMAND ${GIT_EXECUTABLE} update-index --assume-unchanged .vscode/settings.json
            WORKING_DIRECTORY ${PARENT_DIR}
            RESULT_VARIABLE ERRORS)

        if(NOT ERRORS EQUAL "0")
            message("Git assume-unchanged failed: ${ERRORS}")
        endif()
    else()
        message("No Git repository found.")
    endif()
else()
    message("Git executable not found.")
endif()
