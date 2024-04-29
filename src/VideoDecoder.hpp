#pragma once
#include <array>
#include <filesystem>
#include <mutex>
#include <thread>
// TODO way to build Coollab without FFMPEG, and add it to COOLLAB_REQUIRE_ALL_FEATURES
// TODO test that the linux and mac exe work even on a machine that has no ffmpeg installed, and check that they have all the non-lgpl algorithms
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVStream;
struct AVPacket;
struct SwsContext;

namespace ffmpeg {

struct Frame {
    uint8_t* data{};
    int      width{};
    int      height{};
    int      color_channels_count{};
    bool     is_different_from_previous_frame{};
    bool     has_reached_end_of_file{}; /// When we reach the end of the file, we will keep returning the last frame of the file, but you can check this bool and do something different (like displaying nothing, or seeking back to the beginning of the file).
};

enum class SeekMode {
    Exact, /// Returns the exact requested frame.
    Fast,  /// Returns the keyframe just before the requested frame, and then other calls to get_frame_at() will read a few frames quickly, so that we eventually reach the requested frame. Guarantees that get_frame_at() will never take too long to return.
};

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
    /// Frame will be valid until the next call to get_frame_at()
    /// Returns an RGBA frame in sRGB with straight Alpha
    auto get_frame_at(double time_in_seconds, SeekMode) -> Frame;
    // [[nodiscard]] auto current_frame() const -> AVFrame const&; // TODO take a desired format as param

    [[nodiscard]] auto fps() const -> double;
    [[nodiscard]] auto frames_count() const -> int64_t;

private:
    void convert_frame_to_rgba(AVFrame const&) const;

    [[nodiscard]] auto video_stream() const -> AVStream const&;

    /// Throws on error
    /// Returns false when you reached the end of the file and current_frame() is invalid.
    [[nodiscard]] auto decode_next_frame_into(AVFrame*) -> bool;

    auto get_frame_at_impl(double time_in_seconds, SeekMode) -> AVFrame const&;

    static void video_decoding_thread_job(VideoDecoder& This);
    void        mark_alive(size_t frame_index);
    void        mark_dead(size_t frame_index);
    void        mark_all_frames_dead();
    auto        wait_for_dead_frame() -> size_t;
    void        process_packets_until(double time_in_seconds);

    auto present_time(AVFrame const&) const -> double;
    auto present_time(AVPacket const&) const -> double;

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
    std::vector<AVFrame*> _frames{}; // TODO what is a good number ? 5 ? Might be less
    int64_t               _previous_pts{-99999};

    std::atomic<bool> _has_reached_end_of_file{false};
    // Thread
    std::thread             _video_decoding_thread{};
    std::atomic<bool>       _wants_to_stop_video_decoding_thread{false};
    std::atomic<bool>       _wants_to_pause_decoding_thread_asap{false};
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