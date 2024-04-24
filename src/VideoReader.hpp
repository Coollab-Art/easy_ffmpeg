#pragma once
#include <filesystem>

extern "C"
{
#include <libavutil/frame.h> // AVFrame
}

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;
enum AVPixelFormat;
enum AVMediaType;

namespace ffmpeg {

enum class VideoReaderLoopMode {
    None,
    Loop,
    Hold,
};

struct VideoReaderSettings {
    VideoReaderLoopMode loop_mode{VideoReaderLoopMode::Loop};
    float               playback_speed{1.f};
    float               start_time{0.f};

    friend auto operator==(VideoReaderSettings const&, VideoReaderSettings const&) -> bool = default;
    // auto        imgui_widget() -> bool;

    // private:
    //     // Serialization
    //     friend class cereal::access;
    //     template<class Archive>
    //     void serialize(Archive& archive)
    //     {
    //         archive(
    //             cereal::make_nvp("Loop mode", loop_mode),
    //             cereal::make_nvp("Playback speed", playback_speed),
    //             cereal::make_nvp("Start time", start_time)
    //         );
    //     }
};

class Capture { // TODO rename as VideoReaderContext / state / FfmpegContext ?
public:
    explicit Capture(std::filesystem::path const& path);
    ~Capture();

    void move_to_next_frame();
    auto current_frame() const -> AVFrame const&;

private:
    int output_video_frame(AVFrame* frame);
    int output_audio_frame(AVFrame* frame);
    int decode_packet(AVCodecContext* dec, const AVPacket* pkt);
    int open_codec_context(int* stream_idx, AVCodecContext** dec_ctx, AVFormatContext* fmt_ctx, AVMediaType type);

private:
    AVFormatContext* fmt_ctx{};

    AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
    int             width, height;
    AVPixelFormat   pix_fmt;
    AVStream *      video_stream = NULL, *audio_stream = NULL;
    const char*     src_filename       = NULL;
    const char*     video_dst_filename = NULL;
    const char*     audio_dst_filename = NULL;
    FILE*           video_dst_file     = NULL;
    FILE*           audio_dst_file     = NULL;

    uint8_t* video_dst_data[4] = {NULL};
    int      video_dst_linesize[4];
    int      video_dst_bufsize;

    int              video_stream_idx = -1, audio_stream_idx = -1;
    AVFrame*         frame             = NULL;
    mutable AVFrame* rgba_frame        = NULL;
    AVPacket*        pkt               = NULL;
    int              video_frame_count = 0;
    int              audio_frame_count = 0;
};
namespace internal {
// class CaptureState {
// public:
//     static auto create(std::filesystem::path const& path) -> internal::CaptureState;

//     [[nodiscard]] auto get_texture(float time_in_seconds, VideoReaderSettings const& settings, std::filesystem::path const& path) -> Texture const&;

// private:
//     cv::VideoCapture       _capture{};
//     std::optional<Texture> _texture{};
//     double                 _frames_per_second{};
//     int                    _frames_count{};
//     int                    _frame_in_texture{-1};
//     int                    _next_frame_in_capture{0};
// };
} // namespace internal

// class VideoReader {
// public:
//     [[nodiscard]] auto path() const -> std::filesystem::path const& { return _path; }
//     void               set_path(std::filesystem::path path);
//     [[nodiscard]] auto settings() const -> VideoReaderSettings const& { return _settings; }
//     [[nodiscard]] auto settings() -> VideoReaderSettings& { return _settings; }

//     [[nodiscard]] auto get_texture(float time_in_seconds) -> Texture const*;
//     [[nodiscard]] auto get_error() const -> std::optional<std::string> const& { return _error_message; }

//     void move_to_next_frame();
//     void set_time(float time_in_seconds); // TODO rename as seek?

//     auto imgui_widget() -> bool;

//     friend auto operator==(VideoReader const& a, VideoReader const& b) -> bool
//     {
//         return a._settings == b._settings
//                && a._path == b._path;
//     }

// private:
//     void create_capture();

// private:
//     std::filesystem::path _path{};
//     VideoReaderSettings   _settings{};

//     std::optional<internal::CaptureState> _capture_state{};
//     std::optional<std::string>            _error_message{};

//     // private:
//     //     // Serialization
//     //     friend class cereal::access;
//     //     template<class Archive>
//     //     void save(Archive& archive) const
//     //     {
//     //         archive(
//     //             cereal::make_nvp("File Path", _path),
//     //             cereal::make_nvp("Settings", _settings)
//     //         );
//     //     }
//     //     template<class Archive>
//     //     void load(Archive& archive)
//     //     {
//     //         archive(_path, _settings);
//     //         create_capture();
//     //     }
// };

} // namespace ffmpeg
