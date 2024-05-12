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

    function(ffmpeg_copy_libs TARGET)
        Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-windows/bin" "")
        install(DIRECTORY "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-windows/bin/" DESTINATION "bin/")
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

    # message(FFMPEG_FOUND ${FFMPEG_FOUND})
    # message(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES})
    # message(FFMPEG_LINK_LIBRARIES ${FFMPEG_LINK_LIBRARIES})
    # message(FFMPEG_LIBRARY_DIRS ${FFMPEG_LIBRARY_DIRS})
    # message(FFMPEG_LDFLAGS ${FFMPEG_LDFLAGS})
    # message(FFMPEG_LDFLAGS_OTHER ${FFMPEG_LDFLAGS_OTHER})
    # message(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS})
    # message(FFMPEG_CFLAGS ${FFMPEG_CFLAGS})
    # message(FFMPEG_CFLAGS_OTHER ${FFMPEG_CFLAGS_OTHER})

    # target_link_libraries(easy_ffmpeg PUBLIC PkgConfig::FFMPEG)
    foreach(LIB ${FFMPEG_LIBRARIES})
        target_link_libraries(easy_ffmpeg PUBLIC "-l${LIB}")
    endforeach()

    target_include_directories(easy_ffmpeg PUBLIC ${FFMPEG_INCLUDE_DIRS})

    if(APPLE)
        set_target_properties(easy_ffmpeg PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path")
    else()
        set_target_properties(easy_ffmpeg PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN")
    endif()

    foreach(LIB ${FFMPEG_LINK_LIBRARIES})
        message("Installing FFmpeg lib: ${LIB}")
        install(FILES "${LIB}" DESTINATION bin/)
    endforeach()

    set(EASY_FFMPEG_LIBRARIES ${FFMPEG_LINK_LIBRARIES} CACHE STRING "" FORCE)

    function(ffmpeg_copy_libs TARGET)
        foreach(LIB ${EASY_FFMPEG_LIBRARIES})
            Cool__target_copy_file(${TARGET} ${LIB} "")
        endforeach()
    endfunction()
endif()