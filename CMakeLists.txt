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
    link_directories("${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-macos/lib")
    set_target_properties(easy_ffmpeg PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH_USE_LINK_PATH TRUE
        BUILD_RPATH "@executable_path"
        MACOSX_RPATH "@executable_path"
        BUILD_RPATH_USE_ORIGIN TRUE
        INSTALL_RPATH "@executable_path")
    target_link_options(easy_ffmpeg PRIVATE "-L${EASY_FFMPEG_FOLDER}/lib/ffmpeg-macos/lib")

    target_include_directories(easy_ffmpeg SYSTEM PUBLIC lib/ffmpeg-macos/include)

    file(GLOB FFMPEG_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-macos/lib/*.dylib")

    foreach(LIB ${FFMPEG_LIBS})
        get_filename_component(FILENAME ${LIB} NAME)
        message("linking ${FILENAME}")
        target_link_libraries(easy_ffmpeg PUBLIC ${FILENAME})
    endforeach()

    install(DIRECTORY "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-macos/lib" DESTINATION "/")

    function(ffmpeg_copy_libs TARGET)
        Cool__target_copy_folder(${TARGET} "${EASY_FFMPEG_FOLDER}/lib/ffmpeg-macos/lib" "")
        set_target_properties(${TARGET} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH_USE_LINK_PATH TRUE
            BUILD_RPATH "@executable_path"
            MACOSX_RPATH "@executable_path"
            BUILD_RPATH_USE_ORIGIN TRUE
            INSTALL_RPATH "@executable_path")
        target_link_options(${TARGET} PRIVATE "-L${EASY_FFMPEG_FOLDER}/lib/ffmpeg-macos/lib")
    endfunction()
else() # Linux
    link_directories("${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-linux/lib")

    target_include_directories(easy_ffmpeg SYSTEM PUBLIC lib/ffmpeg-linux/include)

    file(GLOB FFMPEG_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-linux/lib/*.so*")

    target_link_libraries(easy_ffmpeg PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg-linux/lib/libswscale.so.5") # Needs to be linked first because linking order matters

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