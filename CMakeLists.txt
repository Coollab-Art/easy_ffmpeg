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

# TODO install license
set(EASY_FFMPEG_FOLDER ${CMAKE_CURRENT_SOURCE_DIR} CACHE STRING "" FORCE)

if(WIN32)
    target_include_directories(easy_ffmpeg SYSTEM PUBLIC lib/ffmpeg-windows/include)

    file(GLOB FFMPEG_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-windows/lib/*.lib")

    foreach(LIB ${FFMPEG_LIBS})
        target_link_libraries(easy_ffmpeg PUBLIC ${LIB})
    endforeach()

    install(DIRECTORY "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-windows/dll/" DESTINATION "bin/")

    function(ffmpeg_copy_libs TARGET)
        Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-windows/dll" "")
    endfunction()

elseif(APPLE)
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
        Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-macos/lib" "FFmpeg")
        set_target_properties(${TARGET} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path/FFmpeg")
    endfunction()
else()
    target_include_directories(easy_ffmpeg SYSTEM PUBLIC lib/ffmpeg-linux/include)

    file(GLOB FFMPEG_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-linux/lib/*.so*")

    foreach(LIB ${FFMPEG_LIBS})
        target_link_libraries(easy_ffmpeg PUBLIC ${LIB})
    endforeach()

    install(DIRECTORY "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-linux/lib" DESTINATION "FFmpeg/")

    function(ffmpeg_copy_libs TARGET)
        Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-linux/lib" "FFmpeg")
        set_target_properties(${TARGET} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN/FFmpeg")
    endfunction()
endif()