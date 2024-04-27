#pragma once
#include <filesystem>
#include <memory>
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

namespace internal {
struct VideoDecoderRaii {
    // Contexts
    AVFormatContext* format_ctx{};
    AVCodecContext*  decoder_ctx{};
    SwsContext*      sws_ctx{};

    // Data
    AVFrame*         frame{};
    mutable AVFrame* rgba_frame{};
    mutable uint8_t* rgba_buffer{};
    AVPacket*        packet{};

    VideoDecoderRaii() = default;
    ~VideoDecoderRaii();
    VideoDecoderRaii(VideoDecoderRaii const&)                        = delete;
    auto operator=(VideoDecoderRaii const&) -> VideoDecoderRaii&     = delete;
    VideoDecoderRaii(VideoDecoderRaii&&) noexcept                    = delete; // No need to implement, since VideoDecoderRaii is always stored in a unique_ptr that handles moving
    auto operator=(VideoDecoderRaii&&) noexcept -> VideoDecoderRaii& = delete; // No need to implement, since VideoDecoderRaii is always stored in a unique_ptr that handles moving
};
} // namespace internal

class VideoDecoder {
public:
    /// Throws if the creation fails (file not found / invalid video file / format not supported, etc.)
    explicit VideoDecoder(std::filesystem::path const& path);

    /// Throws on error
    /// Returns false when you reached the end of the file and current_frame() is invalid.
    [[nodiscard]] auto move_to_next_frame() -> bool;
    void               seek_to(int64_t time_in_nanoseconds); // TODO use AVSEEK_FLAG_BACKWARD to optimize seeking backwards ?
    void               seek_to_start();

    [[nodiscard]] auto current_frame() const -> AVFrame const&; // TODO take a desired format as param // TODO return our own Frame type, that only contains info we now are valid (like width and height that we copy from the other frame)

    [[nodiscard]] auto fps() const -> double;
    [[nodiscard]] auto frames_count() const -> int64_t;

    [[nodiscard]] auto video_stream() const -> AVStream const&;

private:
    void convert_frame_to_rgba() const;

private:
    std::unique_ptr<internal::VideoDecoderRaii> _d{};

    int _video_stream_idx{};
};

} // namespace ffmpeg