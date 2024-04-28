#pragma once
#include <array>
#include <filesystem>
#include <mutex>
#include <thread>
extern "C"
{
#include <libavformat/avformat.h> // AVStream
#include <libavutil/avutil.h>     // AVFrame // TODO which one is the right one ?
#include <libavutil/frame.h>      // AVFrame // TODO which one is the right one ?
}
// TODO way to build Coollab without FFMPEG, and add it to COOLLAB_REQUIRE_ALL_FEATURES
// TODO test that the linux and mac exe work even on a machine that has no ffmpeg installed, and check that they have all the non-lgpl algorithms
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;
struct SwsContext;

namespace ffmpeg {

class VideoDecoder {
public:
    /// Throws if the creation fails (file not found / invalid video file / format not supported, etc.)
    explicit VideoDecoder(std::filesystem::path const& path);
    ~VideoDecoder();
    VideoDecoder(VideoDecoder const&)                        = delete; ///
    auto operator=(VideoDecoder const&) -> VideoDecoder&     = delete; /// Not allowed to copy nor move the class (because we spawn a thread with a reference to this object)
    VideoDecoder(VideoDecoder&&) noexcept                    = delete; /// Always heap-allocate it, typically in a std::unique_ptr
    auto operator=(VideoDecoder&&) noexcept -> VideoDecoder& = delete; ///

    // void seek_to(int64_t time_in_nanoseconds); // TODO use AVSEEK_FLAG_BACKWARD to optimize seeking backwards ?
    // void seek_to_start();
    /// Frame reference will be valid until the next call to get_frame_at()
    auto get_frame_at(double time_in_seconds) -> AVFrame const&;
    // [[nodiscard]] auto current_frame() const -> AVFrame const&; // TODO take a desired format as param // TODO return our own Frame type, that only contains info we now are valid (like width and height that we copy from the other frame)

    [[nodiscard]] auto fps() const -> double;
    [[nodiscard]] auto frames_count() const -> int64_t;

    [[nodiscard]] auto video_stream() const -> AVStream const&;

private:
    void convert_frame_to_rgba(AVFrame const&) const;

    /// Throws on error
    /// Returns false when you reached the end of the file and current_frame() is invalid.
    [[nodiscard]] auto decode_next_frame_into(AVFrame*) -> bool;

    auto get_frame_at_impl(double time_in_seconds) -> AVFrame const&;

    static void video_decoder_thread_job(VideoDecoder& This);
    void        mark_alive(size_t frame_index);
    void        mark_dead(size_t frame_index);
    void        mark_all_frames_dead();
    auto        wait_for_dead_frame() -> size_t;

    auto present_time(AVFrame const& frame) const -> double;

private:
    // Contexts
    AVFormatContext* _format_ctx{};
    AVCodecContext*  _decoder_ctx{};
    SwsContext*      _sws_ctx{};

    // Data
    mutable AVFrame* _rgba_frame{};
    mutable uint8_t* _rgba_buffer{};
    AVPacket*        _packet{};
    /// Always contains the last requested frame, + the frames that will come after that one
    std::array<AVFrame*, 5> _frames{}; // TODO what is a good number ? 5 ? Might be less

    // Thread
    std::thread             _video_decoding_thread{};
    std::atomic<bool>       _wants_to_stop_video_decoding_thread{false};
    std::vector<size_t>     _alive_frames{}; // Always sorted, in order of first frame to present, to latest
    std::mutex              _alive_frames_mutex{};
    std::vector<size_t>     _dead_frames{};
    std::mutex              _dead_frames_mutex{};
    std::mutex              _decoding_context_mutex{};
    std::condition_variable _waiting_for_alive_frames_to_be_filled{};
    std::condition_variable _waiting_for_dead_frames_to_be_filled{};

    // Info
    int _video_stream_idx{};
};

} // namespace ffmpeg