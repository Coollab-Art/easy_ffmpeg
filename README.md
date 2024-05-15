# easy_ffmpeg

This wrapper around FFmpeg libraries makes it very easy to use them in your own project, cross-platform, and even shipping an executable to end users. You can either use it just to include the FFmpeg libraries, or you can also use our wrappers to simplify some common tasks like decoding a video.

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
TODO talk about ffmpeg license (and ourselves we should use GPL, and avoid non-free)
We use a GPL version of FFmpeg, so you are not allowed to use it in a closed-source software.
Then include it as:
```cpp
#include <easy_ffmpeg/easy_ffmpeg.hpp>
```

**NOTE:** Since FFmpeg libs need to be linked dynamically, to ensure that end-users have the FFmpeg libs on their machine we copy them to the folder where your executable is created. If you send your executable to someone, you need to also share the libs. And if you create an installer, it will automatically include the libs so there is nothing to do in that case.

TODO talk about the few ffmpeg tutorials

## Running the tests

Simply use "tests/CMakeLists.txt" to generate a project, then run it.<br/>
If you are using VSCode and the CMake extension, this project already contains a *.vscode/settings.json* that will use the right CMakeLists.txt automatically.
