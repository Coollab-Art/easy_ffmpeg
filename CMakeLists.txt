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
include("CMakeUtils/files_and_folders.cmake")

if(WIN32)
    target_include_directories(easy_ffmpeg SYSTEM PUBLIC lib/ffmpeg-windows/include)

    target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/avcodec.lib)
    target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/avdevice.lib)
    target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/avfilter.lib)
    target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/avformat.lib)
    target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/avutil.lib)
    target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/postproc.lib)
    target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/swresample.lib)
    target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/swscale.lib)

    set(EASY_FFMPEG_FOLDER ${CMAKE_CURRENT_SOURCE_DIR} CACHE STRING "" FORCE)

    function(ffmeg_copy_libs TARGET)
        Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-windows/bin" "")

        # TODO need to INSTALL these files too
    endfunction()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavcodec
        libavdevice
        libavfilter
        libavformat
        libavutil
        libswresample
        libswscale
    )

    target_link_libraries(easy_ffmpeg PUBLIC PkgConfig::LIBAV)

    # TODO will this work when shipping an exe to someone who doesn't have ffmpeg installed?
    function(ffmeg_copy_libs TARGET)
        # Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/windows/ffmpeg/bin" "")
    endfunction()
endif()