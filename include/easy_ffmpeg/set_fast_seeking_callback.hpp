#pragma once
#include <functional>

namespace ffmpeg {

/// Callback called repeatedly while fast-seeking (every time a new frame has been decoded)
void set_fast_seeking_callback(std::function<void()>);

} // namespace ffmpeg