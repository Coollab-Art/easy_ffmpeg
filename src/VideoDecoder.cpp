#include "VideoDecoder.hpp"
// TODO which includes are actually used ?
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace ffmpeg {

VideoDecoder::VideoDecoder(std::filesystem::path const& path)
{
    /* open input file, and allocate format context */
    if (avformat_open_input(&_format_ctx, path.string().c_str(), nullptr, nullptr) < 0)
    {
        fprintf(stderr, "Could not open source file %s\n", path.string().c_str());
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(_format_ctx, nullptr) < 0)
    {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    int ret;
    if (open_codec_context(&_video_stream_idx, &_decoder_ctx, AVMEDIA_TYPE_VIDEO) >= 0)
    {
        _video_stream = _format_ctx->streams[_video_stream_idx];
        _width        = _decoder_ctx->width;
        _height       = _decoder_ctx->height;
        _pixel_format = _decoder_ctx->pix_fmt;
    }

    if (!_video_stream)
    {
        fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
        ret = 1;
        // goto end;
    }

    _frame      = av_frame_alloc();
    _rgba_frame = av_frame_alloc(); // TODO handle error
    if (!_frame)
    {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        // goto end;
    }

    _packet = av_packet_alloc();
    if (!_packet)
    {
        fprintf(stderr, "Could not allocate packet\n");
        ret = AVERROR(ENOMEM);
        // goto end;
    }
}

VideoDecoder::~VideoDecoder()
{
    if (_decoder_ctx)
        avcodec_send_packet(_decoder_ctx, nullptr); // flush the decoder
    avcodec_free_context(&_decoder_ctx);
    avformat_close_input(&_format_ctx);
    av_packet_free(&_packet);
    av_frame_unref(_frame);
    av_frame_unref(_rgba_frame);
    av_frame_free(&_frame);
    av_frame_free(&_rgba_frame);
    av_free(_rgba_buffer);
}

// Function to convert AVFrame to RGBA format
AVFrame* VideoDecoder::convertFrameToRGBA(AVFrame* frame, AVFrame* rgbaFrame) const
{
    // Allocate RGBA frame

    // Set up sws context for conversion
    static SwsContext* swsCtx = sws_getContext( // TODO store in the class
        frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format),
        frame->width, frame->height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsCtx)
    {
        // Handle error
        av_frame_free(&rgbaFrame);
        return nullptr;
    }

    // Allocate RGBA frame buffer
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame->width, frame->height, 1);
    _rgba_buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t)); // TODO only alloc once when creating the class ?
    if (!_rgba_buffer)
    {
        // Handle error
        sws_freeContext(swsCtx);
        av_frame_free(&rgbaFrame);
        return nullptr;
    }

    // Assign RGBA frame buffer
    av_image_fill_arrays(rgbaFrame->data, rgbaFrame->linesize, _rgba_buffer, AV_PIX_FMT_RGBA, frame->width, frame->height, 1);

    // Convert frame to RGBA
    sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height, rgbaFrame->data, rgbaFrame->linesize);

    // Clean up
    // av_free(_rgba_buffer);
    // sws_freeContext(swsCtx); // TODO

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

int VideoDecoder::open_codec_context(int* stream_idx, AVCodecContext** dec_ctx, AVMediaType type)
{
    int            ret, stream_index;
    AVStream*      st;
    const AVCodec* dec = NULL;

    ret = av_find_best_stream(_format_ctx, type, -1, -1, NULL, 0);
    if (ret < 0)
    {
        fprintf(stderr, "Could not find %s stream\n", av_get_media_type_string(type));
        return ret;
    }
    else
    {
        stream_index = ret;
        st           = _format_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec)
        {
            fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx)
        {
            fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0)
        {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders */
        if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0)
        {
            fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

void VideoDecoder::move_to_next_frame()
{
    av_frame_unref(_frame); // Delete previous frame // TODO might not be needed, because avcodec_receive_frame() already calls av_frame_unref at the beginning
    av_free(_rgba_buffer);
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

        convertFrameToRGBA(_frame, _rgba_frame); // TODO only convert if it doesn"t exist yet // TODO add param to choose color spae, and store a map of all frames in all color spaces that have been requested
    return *_rgba_frame;
}

} // namespace ffmpeg
