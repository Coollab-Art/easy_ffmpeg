#pragma once
#include <functional>
#include <string>

namespace ffmpeg {

/// Callback called repeatedly while fast-seeking (every time a new frame has been decoded)
/// NB: this will be called from a separate thread, so make sure your callback is thread-safe!
void set_fast_seeking_callback(std::function<void()>);

/// Callback called whenever an error occurs while decoding a frame from the video (which shouldn't happen, unless your video file is corrupted)
/// NB: this will NOT report errors that occur during the construction of a VideoDecoder (i.e. when we first open the file, and check that it exists and is a supported video format) Those errors are thrown as exceptions instead.
/// NB: this will be called from a separate thread, so make sure your callback is thread-safe!
void set_frame_decoding_error_callback(std::function<void(std::string const& error_message)>);

} // namespace ffmpeg