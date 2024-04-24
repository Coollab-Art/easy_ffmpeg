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
target_include_directories(easy_ffmpeg PUBLIC lib/ffmpeg/include)
target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg/lib/avcodec.lib)
target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg/lib/avdevice.lib)
target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg/lib/avfilter.lib)
target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg/lib/avformat.lib)
target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg/lib/avutil.lib)
target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg/lib/postproc.lib)
target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg/lib/swresample.lib)
target_link_libraries(easy_ffmpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg/lib/swscale.lib)
include("CMakeUtils/files_and_folders.cmake")

set(EASY_FFMPEG_FOLDER ${CMAKE_CURRENT_SOURCE_DIR} CACHE STRING "" FORCE)

function(ffmeg_copy_libs TARGET)
    Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/lib/ffmpeg/bin" "")
endfunction()