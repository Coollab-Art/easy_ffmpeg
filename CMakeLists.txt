cmake_minimum_required(VERSION 3.8)

set(WARNINGS_AS_ERRORS_FOR_EASY_FFMPEG OFF CACHE BOOL "ON iff you want to treat warnings as errors")

add_library(easy_ffmpeg)
add_library(easy_ffmpeg::easy_ffmpeg ALIAS easy_ffmpeg)
target_compile_features(easy_ffmpeg PUBLIC cxx_std_20)

# ---Add source files---
if(WARNINGS_AS_ERRORS_FOR_EASY_FFMPEG)
    target_include_directories(easy_ffmpeg PUBLIC include)
else()
    target_include_directories(easy_ffmpeg SYSTEM PUBLIC include)
endif()

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/*.cpp)
target_sources(easy_ffmpeg PRIVATE ${SRC_FILES})

# ---Set warning level---
if(MSVC)
    target_compile_options(easy_ffmpeg PRIVATE /W4)
else()
    target_compile_options(easy_ffmpeg PRIVATE -Wall -Wextra -Wpedantic -pedantic-errors -Wconversion -Wsign-conversion -Wimplicit-fallthrough)
endif()

# ---Maybe enable warnings as errors---
if(WARNINGS_AS_ERRORS_FOR_EASY_FFMPEG)
    if(MSVC)
        target_compile_options(easy_ffmpeg PRIVATE /WX)
    else()
        target_compile_options(easy_ffmpeg PRIVATE -Werror)
    endif()
endif()

# ---Add ffmpeg---
install(FILES "lib/FFmpeg/LICENSE" DESTINATION "license/FFmpeg")

if(WIN32)
    target_include_directories(easy_ffmpeg SYSTEM PUBLIC lib/FFmpeg/windows/include)

    file(GLOB FFMPEG_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/FFmpeg/windows/lib/*.lib")

    foreach(LIB ${FFMPEG_LIBS})
        target_link_libraries(easy_ffmpeg PUBLIC ${LIB})
    endforeach()

    set(EASY_FFMPEG_FOLDER ${CMAKE_CURRENT_SOURCE_DIR} CACHE STRING "" FORCE)
    include("CMakeUtils/files_and_folders.cmake")

    function(ffmpeg_copy_libs TARGET)
        Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/lib/FFmpeg/windows/bin" "")
        install(DIRECTORY "${EASY_FFMPEG_FOLDER}/lib/FFmpeg/windows/bin/" DESTINATION "bin/")
    endfunction()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec
        libavdevice
        libavfilter
        libavformat
        libavutil
        libswresample
        libswscale
    )
    target_link_libraries(easy_ffmpeg PUBLIC PkgConfig::FFMPEG)

    function(ffmpeg_copy_libs TARGET)
        # No need to do anything, we will find the libs installed globally # NB: this is not ideal because it requires every user of an application using this library to install FFmpeg separately. See the work going on in the test-linking-lib branch.
    endfunction()
endif()