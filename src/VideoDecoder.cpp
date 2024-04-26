#include "VideoDecoder.hpp"
#include <array>
#include <cassert>
#include <format>
#include <iostream> // TODO remove
#include <stdexcept>

// TODO which includes are actually used ?
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
// TODO crash when video ends
namespace ffmpeg {

static void throw_error(std::string const& message)
{
    std::cout << message << '\n'; // TODO remove
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
    int err{};

    err = avformat_open_input(&_format_ctx, path.string().c_str(), nullptr, nullptr);
    if (err < 0)
        throw_error(std::format("Could not open video file \"{}\"", path.string()), err);

    err = avformat_find_stream_info(_format_ctx, nullptr);
    if (err < 0)
        throw_error(std::format("Could not find stream information in video file \"{}\"", path.string()), err);

    open_codec_context(&_video_stream_idx, &_decoder_ctx);

    _frame      = av_frame_alloc();
    _rgba_frame = av_frame_alloc();
    _packet     = av_packet_alloc();
    if (!_frame || !_rgba_frame || !_packet)
        throw_error(std::format("Not enough memory to open video file \"{}\"", path.string()));

    // TODO convert to sRGB (I think AV_PIX_FMT_RGBA is linear rgb)
    auto const& params = *_format_ctx->streams[_video_stream_idx]->codecpar; // NOLINT(*pointer-arithmetic)
    _sws_ctx           = sws_getContext(
        params.width, params.height,
        static_cast<AVPixelFormat>(params.format),
        params.width, params.height,
        AV_PIX_FMT_RGBA,
        0, nullptr, nullptr, nullptr
    );
    if (!_sws_ctx)
        throw_error(std::format("Failed to create conversion context for video file \"{}\"", path.string()));

    // Allocate RGBA frame buffer
    _rgba_buffer = static_cast<uint8_t*>(av_malloc(sizeof(uint8_t) * av_image_get_buffer_size(AV_PIX_FMT_RGBA, params.width, params.height, 1)));
    if (!_rgba_buffer)
        throw_error(std::format("Not enough memory to open video file \"{}\"", path.string()));

    // Assign RGBA frame buffer
    av_image_fill_arrays(_rgba_frame->data, _rgba_frame->linesize, _rgba_buffer, AV_PIX_FMT_RGBA, params.width, params.height, 1);
}

VideoDecoder::~VideoDecoder()
{
    if (_decoder_ctx)
        avcodec_send_packet(_decoder_ctx, nullptr); // Flush the decoder
    avcodec_free_context(&_decoder_ctx);
    avformat_close_input(&_format_ctx);
    av_packet_free(&_packet);
    av_frame_unref(_frame);
    av_frame_unref(_rgba_frame);
    av_frame_free(&_frame);
    av_frame_free(&_rgba_frame);
    av_free(_rgba_buffer);
    sws_freeContext(_sws_ctx);
}

void VideoDecoder::convert_frame_to_rgba(AVFrame* frame, AVFrame* rgbaFrame) const
{
    sws_scale(_sws_ctx, frame->data, frame->linesize, 0, frame->height, rgbaFrame->data, rgbaFrame->linesize);
    rgbaFrame->width  = frame->width;
    rgbaFrame->height = frame->height;
    rgbaFrame->format = frame->format;
}

int VideoDecoder::decode_packet()
{
    int ret = 0;

    // submit the packet to the decoder
    ret = avcodec_send_packet(_decoder_ctx, _packet);
    if (ret < 0)
    {
        // std::cerr << "Error submitting a packet for decoding " << av_err2str(ret) << '\n'; // TODO
        return ret;
    }

    // get all the available frames from the decoder
    if (ret >= 0)
    {
        ret = avcodec_receive_frame(_decoder_ctx, _frame);
        if (ret < 0)
        {
            // those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            // fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret)); // TODO
            return ret;
        }

        // av_frame_unref(frame);
        if (ret < 0)
            return ret;
    }

    return 0;
}

void VideoDecoder::open_codec_context(int* stream_idx, AVCodecContext** dec_ctx)
{
    AVStream*      st;
    const AVCodec* dec = NULL;

    int err{};
    err = av_find_best_stream(_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (err < 0)
        throw_error("Could not find video stream. Make sure your file is a video file and not an audio file", err);

    int const stream_index = err;
    st                     = _format_ctx->streams[stream_index];

    // Find decoder for the stream
    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec)
    {
        // st->codecpar->codec_tag
        // TODO log codec name in error
        throw_error(std::format("Failed to find video codec \"{}\"", "bob"));
    }

    /* Allocate a codec context for the decoder */
    auto type = AVMEDIA_TYPE_VIDEO; // TODO remove
    *dec_ctx  = avcodec_alloc_context3(dec);
    if (!*dec_ctx)
    {
        fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(type));
        // return AVERROR(ENOMEM);
    }

    /* Copy codec parameters from input stream to output codec context */
    if ((err = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0)
    {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(type));
        // return ret;
    }

    /* Init the decoders */
    if ((err = avcodec_open2(*dec_ctx, dec, NULL)) < 0)
    {
        fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(type));
        // return ret;
    }
    *stream_idx = stream_index;
}

void VideoDecoder::move_to_next_frame()
{
    av_frame_unref(_frame); // Delete previous frame // TODO might not be needed, because avcodec_receive_frame() already calls av_frame_unref at the beginning
    int  ret;
    bool found = false;
    while (!found)
    {
        if (av_read_frame(_format_ctx, _packet) < 0)
        {
            av_packet_unref(_packet);
            break;
        }

        // check if the packet belongs to a stream we are interested in, otherwise
        // skip it
        // TODO what does it mean ? Should we then try to read the frame after that one ? (NB: I think so, since a packet will only be video OR audio, every other packet is probably an audio packet)
        if (_packet->stream_index == _video_stream_idx)
        {
            ret   = decode_packet();
            found = true;
        }
        // else if (_packet->stream_index == audio_stream_idx)
        //     ret = decode_packet(audio_dec_ctx, _packet);
        av_packet_unref(_packet);
        // if (ret < 0) // TODO File end, handle this
        //     break;
    }
}

auto VideoDecoder::current_frame() const -> AVFrame const&
{
    if (_frame->width != 0) // TODO this should never happen ?

        convert_frame_to_rgba(_frame, _rgba_frame); // TODO only convert if it doesn"t exist yet // TODO add param to choose color spae, and store a map of all frames in all color spaces that have been requested
    return *_rgba_frame;
}

} // namespace ffmpeg
