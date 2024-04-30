# easy_ffmpeg

## Including

To add this library to your project, simply add these two lines to your *CMakeLists.txt*:
```cmake
add_subdirectory(path/to/easy_ffmpeg)
target_link_libraries(${PROJECT_NAME} PRIVATE easy_ffmpeg::easy_ffmpeg)
```

On Linux, you will need to install the FFMPEG libraries with
```bash
sudo apt install libavcodec-dev=6.0 libavdevice-dev=6.0 libavfilter-dev=6.0 libavformat-dev=6.0 libavutil-dev=6.0 libpostproc-dev=6.0 libswresample-dev=6.0 libswscale-dev=6.0
```
On Mac, you will need to install the FFMPEG libraries with
```bash
brew install ffmpeg@6.0
```

Then include it as:
```cpp
#include <easy_ffmpeg/easy_ffmpeg.hpp>
```

## Running the tests

Simply use "tests/CMakeLists.txt" to generate a project, then run it.<br/>
If you are using VSCode and the CMake extension, this project already contains a *.vscode/settings.json* that will use the right CMakeLists.txt automatically.
