# easy_ffmpeg

## Including

To add this library to your project, simply add these three lines to your *CMakeLists.txt*:
```cmake
add_subdirectory(path/to/easy_ffmpeg)
target_link_libraries(${PROJECT_NAME} PRIVATE easy_ffmpeg::easy_ffmpeg)
ffmpeg_copy_libs(${PROJECT_NAME})
```

On Linux, you will need to install the FFMPEG libraries with
```bash
sudo apt-get install libavcodec-dev libavdevice-dev libavfilter-dev libavformat-dev libavutil-dev libpostproc-dev libswresample-dev libswscale-dev
```
On Mac, you will need to install the FFMPEG libraries with
```bash
brew install ffmpeg
```

Then include it as:
```cpp
#include <easy_ffmpeg/easy_ffmpeg.hpp>
```

## Running the tests

Simply use "tests/CMakeLists.txt" to generate a project, then run it.<br/>
If you are using VSCode and the CMake extension, this project already contains a *.vscode/settings.json* that will use the right CMakeLists.txt automatically.
