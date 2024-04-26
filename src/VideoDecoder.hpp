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

class VideoDecoder {
public:
    explicit VideoDecoder(std::filesystem::path const& path);
    ~VideoDecoder();

    void move_to_next_frame();
    auto current_frame() const -> AVFrame const&;

private:
    int      output_video_frame(AVFrame* frame);
    int      output_audio_frame(AVFrame* frame);
    int      decode_packet(AVCodecContext* dec, const AVPacket* pkt);
    int      open_codec_context(int* stream_idx, AVCodecContext** dec_ctx, AVFormatContext* fmt_ctx, AVMediaType type);
    AVFrame* convertFrameToRGBA(AVFrame* frame, AVFrame* rgbaFrame) const;

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
    AVFrame*         frame      = NULL;
    mutable AVFrame* rgba_frame = NULL;
    mutable uint8_t* rgbaBuffer{};
    AVPacket*        pkt               = NULL;
    int              video_frame_count = 0;
    int              audio_frame_count = 0;
};

} // namespace ffmpeg