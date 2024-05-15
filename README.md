# easy_ffmpeg

This wrapper around FFmpeg libraries makes it very easy to use them in your own project, cross-platform, and even shipping an executable to end users. You can either use it just to include the FFmpeg libraries, or you can also use our wrappers to simplify some common tasks like decoding a video.    

## Including

To add this library to your project, simply add these three lines to your *CMakeLists.txt*:
```cmake
add_subdirectory(path/to/easy_ffmpeg)
target_link_libraries(${PROJECT_NAME} PRIVATE easy_ffmpeg::easy_ffmpeg)
ffmpeg_copy_libs(${PROJECT_NAME}) # This will make sure the shared libraries get installed next to the executable.
```

**On Linux**, you will also need to install the FFMPEG libraries with
```bash
sudo apt-get install libavcodec-dev libavdevice-dev libavfilter-dev libavformat-dev libavutil-dev libpostproc-dev libswresample-dev libswscale-dev
```
**On Mac**, you will also need to install the FFMPEG libraries with
```bash
brew install ffmpeg
```

Then include it as:
```cpp
#include <easy_ffmpeg/easy_ffmpeg.hpp>
```

## Learning about FFmpeg

If you need to use the raw FFmpeg API, here are the few tutorials that we managed to find:

- [Decent intro to the basics of FFmpeg](https://github.com/leandromoreira/ffmpeg-libav-tutorial?tab=readme-ov-file#chapter-0---the-infamous-hello-world)
- [More detailed tutorial (but a bit deprecated but it's okay)](http://dranger.com/ffmpeg/)

## Shipping an application

⚠ You need to be mindful of the LICENSE of FFmpeg. This wrapper provides a GPL version of FFmpeg on Windows, so you are not allowed to use it in a closed-source software (you can replace the libs in *lib/FFmpeg/windows* if you need to, and the LICENSE in *lib/FFmpeg*).
**There are also several other things to do** to comply with their license, see https://ffmpeg.org/legal.html

**NOTE:** Since FFmpeg libs need to be linked dynamically:
- **On Windows** to ensure that end-users have the FFmpeg libs on their machine we copy them to the folder where your executable is created. If you send your executable to someone, you need to also share the libs. And if you create an installer, it will automatically include the libs so there is nothing to do in that case.
- On **Linux and MacOS** you need to tell your users to install FFMpeg manually, with `sudo apt-get install ffmpeg` on Linux and `brew install ffmpeg` on MacOS (+ [install Homebrew](https://brew.sh/) if they don't have it already)

## Running the tests

Simply use "tests/CMakeLists.txt" to generate a project, then run it.<br/>
If you are using VSCode and the CMake extension, this project already contains a *.vscode/settings.json* that will use the right CMakeLists.txt automatically.
