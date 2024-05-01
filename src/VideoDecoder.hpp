#pragma once
extern "C"
{
#include <libavutil/pixfmt.h>
}
#include <filesystem>
#include <functional>
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
    bool     is_last_frame{}; /// If this is the last frame in the file, we will keep returning it, but you can might want to do something else (like displaying nothing, or seeking back to the beginning of the file).
};

enum class SeekMode {
    Exact, /// Returns the exact requested frame.
    Fast,  /// Returns the keyframe just before the requested frame, and then other calls to get_frame_at() will read a few frames quickly, so that we eventually reach the requested frame. Guarantees that get_frame_at() will never take too long to return.
};

/// Callback called repeatedly while fast-seeking (every time a new frame has been decoded)
void set_fast_seeking_callback(std::function<void()>);

class VideoDecoder {
public:
    /// Throws if the creation fails (file not found / invalid video file / format not supported, etc.)
    explicit VideoDecoder(std::filesystem::path const& path, AVPixelFormat);
    ~VideoDecoder();
    VideoDecoder(VideoDecoder const&)                        = delete; ///
    auto operator=(VideoDecoder const&) -> VideoDecoder&     = delete; /// Not allowed to copy nor move the class (because we spawn a thread with a reference to this object)
    VideoDecoder(VideoDecoder&&) noexcept                    = delete; /// Always heap-allocate it, typically in a std::unique_ptr
    auto operator=(VideoDecoder&&) noexcept -> VideoDecoder& = delete; ///

    /// Frame will be valid until the next call to get_frame_at()
    /// Returns an RGBA frame in sRGB with straight Alpha
    auto get_frame_at(double time_in_seconds, SeekMode) -> Frame;

    [[nodiscard]] auto duration_in_seconds() const -> double;

    auto detailed_info() const -> std::string const& { return _detailed_info; }

private:
    void convert_frame_to_desired_color_space(AVFrame const&) const;

    [[nodiscard]] auto video_stream() const -> AVStream const&;

    /// Throws on error
    /// Returns false when you reached the end of the file and current_frame() is invalid.
    [[nodiscard]] auto decode_next_frame_into(AVFrame*) -> bool;

    auto get_frame_at_impl(double time_in_seconds, SeekMode) -> AVFrame const&;

    static void video_decoding_thread_job(VideoDecoder& This);
    void        process_packets_until(double time_in_seconds);

    auto present_time(AVFrame const&) const -> double;
    auto present_time(AVPacket const&) const -> double;

    auto seeking_would_move_us_forward(double time_in_seconds) -> bool;

    auto retrieve_detailed_info() const -> std::string;

private:
    // Contexts
    AVFormatContext* _format_ctx{};
    AVFormatContext* _test_seek_format_ctx{}; // Dummy context that we use to seek and check that a seek would actually bring us closer to the frame we want to reach (which is not the case when the closest keyframe to the frame we seek is before the frame we are currently decoding)
    AVCodecContext*  _decoder_ctx{};
    SwsContext*      _sws_ctx{};

    std::optional<double> _seek_target{};
    // Data
    mutable AVFrame* _desired_color_space_frame{};
    mutable uint8_t* _desired_color_space_buffer{};
    AVPacket*        _packet{};
    AVPacket*        _test_seek_packet{}; // Dummy packet that we use to seek and check that a seek would actually bring us closer to the frame we want to reach (which is not the case when the closest keyframe to the frame we seek is before the frame we are currently decoding)
    /// Always contains the last requested frame, + the frames that will come after that one
    class FramesQueue {
    public:
        FramesQueue();
        ~FramesQueue();
        FramesQueue(FramesQueue const&)                        = delete;
        auto operator=(FramesQueue const&) -> FramesQueue&     = delete;
        FramesQueue(FramesQueue&&) noexcept                    = delete;
        auto operator=(FramesQueue&&) noexcept -> FramesQueue& = delete;

        [[nodiscard]] auto size() -> size_t;
        [[nodiscard]] auto size_no_lock() -> size_t;
        [[nodiscard]] auto is_full() -> bool;
        [[nodiscard]] auto is_empty() -> bool;

        [[nodiscard]] auto first() -> AVFrame const&;
        [[nodiscard]] auto second() -> AVFrame const&;
        // [[nodiscard]] auto last() -> AVFrame const&;
        [[nodiscard]] auto get_frame_to_fill() -> AVFrame*;

        void push(AVFrame*);
        void pop();
        void clear();

        auto waiting_for_queue_to_fill_up() -> std::condition_variable& { return _waiting_for_push; }
        auto waiting_for_queue_to_empty_out() -> std::condition_variable& { return _waiting_for_pop; }

        auto mutex() -> std::mutex& { return _mutex; }

    private:
        std::vector<AVFrame*> _alive_frames{};
        std::vector<AVFrame*> _dead_frames{};
        std::mutex            _mutex{};

        std::condition_variable _waiting_for_push{};
        std::condition_variable _waiting_for_pop{};
    };
    FramesQueue _frames_queue{};
    int64_t     _previous_pts{-99999};

    std::atomic<bool> _has_reached_end_of_file{false};
    // Thread
    std::thread         _video_decoding_thread{};
    std::atomic<bool>   _wants_to_stop_video_decoding_thread{false};
    std::atomic<bool>   _wants_to_pause_decoding_thread_asap{false};
    std::vector<size_t> _alive_frames{}; // Always sorted, in order of first frame to present, to latest
    std::vector<size_t> _dead_frames{};
    // std::mutex              _dead_frames_mutex{};
    std::mutex _decoding_context_mutex{};

    // Info
    int         _video_stream_idx{};
    std::string _detailed_info{};
};

} // namespace ffmpeg