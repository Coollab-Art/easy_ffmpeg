#include "VideoDecoder.hpp"
#include <array>
#include <cassert>
#include <stdexcept>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace ffmpeg {

static void throw_error(std::string const& message)
{
    throw std::runtime_error(message);
}

static void throw_error(std::string message, int err)
{
    assert(err < 0);
    auto err_str_buffer = std::array<char, AV_ERROR_MAX_STRING_SIZE>{};
    av_strerror(err, err_str_buffer.data(), AV_ERROR_MAX_STRING_SIZE);
    message += ":\n";
    message += err_str_buffer.data();

    throw_error(message);
}

VideoDecoder::VideoDecoder(std::filesystem::path const& path)
{
    _d = std::make_unique<internal::VideoDecoderRaii>();

    {
        int const err = avformat_open_input(&_d->format_ctx, path.string().c_str(), nullptr, nullptr);
        if (err < 0)
            throw_error("Could not open file. Make sure the path is valid and is an actual video file", err);
    }

    {
        int const err = avformat_find_stream_info(_d->format_ctx, nullptr);
        if (err < 0)
            throw_error("Could not find stream information. Your file is most likely corrupted or not a valid video file", err);
    }

    {
        int const err = av_find_best_stream(_d->format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (err < 0)
            throw_error("Could not find video stream. Make sure your file is a video file and not an audio file", err);

        _video_stream_idx = err;
    }

    auto const& params = *video_stream().codecpar;

    AVCodec const* decoder = avcodec_find_decoder(params.codec_id);
    if (!decoder)
    {
        auto const* desc = avcodec_descriptor_get(params.codec_id);
        throw_error("Codec \"" + std::string{desc ? desc->name : "Unknown"} + "\" is not supported (" + std::string{desc ? desc->long_name : "Unknown"} + ")");
    }

    _d->decoder_ctx = avcodec_alloc_context3(decoder);
    if (!_d->decoder_ctx)
        throw_error("Not enough memory to open the video file");

    {
        int const err = avcodec_parameters_to_context(_d->decoder_ctx, &params);
        if (err < 0)
            throw_error("Failed to copy codec parameters to decoder context", err);
    }

    {
        int const err = avcodec_open2(_d->decoder_ctx, decoder, nullptr);
        if (err < 0)
        {
            auto const* desc = avcodec_descriptor_get(params.codec_id);
            throw_error("Failed to open codec \"" + std::string{desc ? desc->name : "Unknown"} + "\" (" + std::string{desc ? desc->long_name : "Unknown"} + ")", err);
        }
    }

    _d->frame      = av_frame_alloc();
    _d->rgba_frame = av_frame_alloc();
    _d->packet     = av_packet_alloc();
    if (!_d->frame || !_d->rgba_frame || !_d->packet)
        throw_error("Not enough memory to open the video file");

    // TODO convert to sRGB (I think AV_PIX_FMT_RGBA is linear rgb)
    _d->sws_ctx = sws_getContext(
        params.width, params.height,
        static_cast<AVPixelFormat>(params.format),
        params.width, params.height,
        AV_PIX_FMT_RGBA,
        0, nullptr, nullptr, nullptr
    );
    if (!_d->sws_ctx)
        throw_error("Failed to create RGBA conversion context");

    _d->rgba_buffer = static_cast<uint8_t*>(av_malloc(sizeof(uint8_t) * static_cast<size_t>(av_image_get_buffer_size(AV_PIX_FMT_RGBA, params.width, params.height, 1))));
    if (!_d->rgba_buffer)
        throw_error("Not enough memory to open the video file");

    {
        int const err = av_image_fill_arrays(_d->rgba_frame->data, _d->rgba_frame->linesize, _d->rgba_buffer, AV_PIX_FMT_RGBA, params.width, params.height, 1);
        if (err < 0)
            throw_error("Failed to setup image arrays", err);
    }
}

internal::VideoDecoderRaii::~VideoDecoderRaii()
{
    if (decoder_ctx)
        avcodec_send_packet(decoder_ctx, nullptr); // Flush the decoder
    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&format_ctx);
    av_packet_free(&packet);
    av_frame_unref(frame);
    av_frame_unref(rgba_frame);
    av_frame_free(&frame);
    av_frame_free(&rgba_frame);
    av_free(rgba_buffer);
    sws_freeContext(sws_ctx);
}

void VideoDecoder::convert_frame_to_rgba() const
{
    sws_scale(_d->sws_ctx, _d->frame->data, _d->frame->linesize, 0, _d->frame->height, _d->rgba_frame->data, _d->rgba_frame->linesize);
    _d->rgba_frame->width  = _d->frame->width;
    _d->rgba_frame->height = _d->frame->height;
    _d->rgba_frame->format = _d->frame->format;
}

namespace {
struct PacketRaii { // NOLINT(*special-member-functions)
    AVPacket* packet;

    ~PacketRaii()
    {
        av_packet_unref(packet);
    }
};
} // namespace

auto VideoDecoder::move_to_next_frame() -> bool
{
    // av_frame_unref(_frame); // Delete previous frame // TODO might not be needed, because avcodec_receive_frame() already calls av_frame_unref at the beginning

    bool found = false;
    while (!found)
    {
        PacketRaii packet_raii{_d->packet}; // Will unref the packet when exiting the scope

        { // Reads data from the file and puts it in the packet (most of the time this will be the actual video frame, but it can also be additional data, in which case avcodec_receive_frame() will return AVERROR(EAGAIN))
            int const err = av_read_frame(_d->format_ctx, _d->packet);
            if (err == AVERROR_EOF)
                return false;
            if (err < 0)
                throw_error("Failed to read video packet", err);
        }

        // Check if the packet belongs to the video stream, otherwise skip it
        if (_d->packet->stream_index != _video_stream_idx)
            continue;

        { // Send the packet to the decoder
            int const err = avcodec_send_packet(_d->decoder_ctx, _d->packet);
            assert(err != AVERROR(EAGAIN)); // "input is not accepted in the current state - user must read output with avcodec_receive_frame()" Should never happen for video packets, they always contain only one frame
            assert(err != AVERROR_EOF);     // "the decoder has been flushed, and no new packets can be sent to it" Should never happen if we do our job properly
            assert(err != AVERROR(EINVAL)); // "codec not opened, it is an encoder, or requires flush" Should never happen if we do our job properly
            if (err < 0)
                throw_error("Error submitting a video packet for decoding", err);
        }

        { // Read a frame from the packet that was sent to the decoder. For video streams a packet only contains one frame so no need to call avcodec_receive_frame() in a loop
            int const err = avcodec_receive_frame(_d->decoder_ctx, _d->frame);
            // assert(err != AVERROR(EAGAIN)); // Actually this can happen, if the frame in the packet is not a video frame, but just some extra information
            assert(err != AVERROR_EOF);            // "the codec has been fully flushed, and there will be no more output frames" Should never happen if we do our job properly
            assert(err != AVERROR(EINVAL));        // "codec not opened, or it is an encoder without the AV_CODEC_FLAG_RECON_FRAME flag enabled" Should never happen if we do our job properly
            if (err < 0 && err != AVERROR(EAGAIN)) // EAGAIN is a special error that is not a real problem, we just need to resend a packet
                throw_error("Error while decoding the video", err);

            found = err != AVERROR(EAGAIN);
        }
    }

    return true;
}

void VideoDecoder::seek_to(int64_t time_in_nanoseconds)
{
    // TODO video_stream().start_time
    auto const timestamp = time_in_nanoseconds * video_stream().time_base.den / video_stream().time_base.num / 1'000'000'000;
    // std::cout << timestamp << ' ' << video_stream().time_base.num << ' ' << video_stream().time_base.den << '\n';
    // av_seek_frame(_d->format_ctx, _video_stream_idx, timestamp, AVSEEK_FLAG_ANY);
    // avcodec_flush_buffers(_decoder_ctx);
    // move_to_next_frame();
    int const err = avformat_seek_file(_d->format_ctx, _video_stream_idx, timestamp, timestamp, timestamp, AVSEEK_FLAG_ANY);
    if (err < 0)
        throw_error("Failed to seek to " + std::to_string(time_in_nanoseconds) + " nanoseconds", err);
    // avcodec_flush_buffers(_decoder_ctx);
}

void VideoDecoder::seek_to_start()
{
    int const err = avformat_seek_file(_d->format_ctx, _video_stream_idx, 0, 0, 0, 0);
    if (err < 0)
        throw_error("Failed to seek to the start", err);
    avcodec_flush_buffers(_d->decoder_ctx);
}

auto VideoDecoder::current_frame() const -> AVFrame const&
{
    // assert(_d->frame->width != 0 && _d->frame->height != 0); // TODO handle calls of current_frame() when end of file has been reached
    convert_frame_to_rgba(); // TODO only convert if it doesn"t exist yet // TODO add param to choose color spae, and store a map of all frames in all color spaces that have been requested
    return *_d->rgba_frame;
}

auto VideoDecoder::video_stream() const -> AVStream const&
{
    return *_d->format_ctx->streams[_video_stream_idx]; // NOLINT(*pointer-arithmetic)
}

[[nodiscard]] auto VideoDecoder::fps() const -> double
{
    return av_q2d(video_stream().avg_frame_rate);
}

[[nodiscard]] auto VideoDecoder::frames_count() const -> int64_t
{
    auto const count = video_stream().nb_frames;
    if (count != 0)
        return count;

    // nb_frames is not set or accurate, calculate total frames from duration and framerate
    assert(false); // TODO remove, i just want to see if this procs sometimes
    AVRational const frameRate = video_stream().avg_frame_rate;
    int64_t const    duration  = _d->format_ctx->duration / AV_TIME_BASE;
    return av_rescale(duration, frameRate.num, frameRate.den);
}

} // namespace ffmpeg
