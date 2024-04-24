#include "VideoReader.hpp"
extern "C"
{
    // TODO which includes are actually used ?
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace ffmpeg {

extern "C"
{
#include <libswscale/swscale.h>
}

// Function to convert AVFrame to RGBA format
AVFrame* convertFrameToRGBA(AVFrame* frame, AVFrame* rgbaFrame)
{
    // Allocate RGBA frame

    // Set up sws context for conversion
    SwsContext* swsCtx = sws_getContext(
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
    int      numBytes   = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame->width, frame->height, 1);
    uint8_t* rgbaBuffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    if (!rgbaBuffer)
    {
        // Handle error
        sws_freeContext(swsCtx);
        av_frame_free(&rgbaFrame);
        return nullptr;
    }

    // Assign RGBA frame buffer
    av_image_fill_arrays(rgbaFrame->data, rgbaFrame->linesize, rgbaBuffer, AV_PIX_FMT_RGBA, frame->width, frame->height, 1);

    // Convert frame to RGBA
    sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height, rgbaFrame->data, rgbaFrame->linesize);

    // Clean up
    // av_free(rgbaBuffer);
    sws_freeContext(swsCtx);

    rgbaFrame->width  = frame->width;
    rgbaFrame->height = frame->height;
    rgbaFrame->format = frame->format;
}

int Capture::output_video_frame(AVFrame* frame)
{
    if (frame->width != width || frame->height != height || frame->format != pix_fmt)
    {
        /* To handle this change, one could call av_image_alloc again and
         * decode the following frames into another rawvideo file. */
        fprintf(stderr,
                "Error: Width, height and pixel format have to be "
                "constant in a rawvideo file, but the width, height or "
                "pixel format of the input video changed:\n"
                "old: width = %d, height = %d, format = %s\n"
                "new: width = %d, height = %d, format = %s\n",
                width, height, av_get_pix_fmt_name(pix_fmt), frame->width, frame->height, av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)));
        return -1;
    }

    printf("video_frame n:%d\n", video_frame_count++);

    /* copy decoded frame to destination buffer:
     * this is required since rawvideo expects non aligned data */
    av_image_copy2(video_dst_data, video_dst_linesize, frame->data, frame->linesize, pix_fmt, width, height);

    /* write to rawvideo file */
    fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
    return 0;
}

int Capture::output_audio_frame(AVFrame* frame)
{
    size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));
    // printf("audio_frame n:%d nb_samples:%d pts:%s\n", audio_frame_count++, frame->nb_samples, av_ts2timestr(frame->pts, &audio_dec_ctx->time_base));

    /* Write the raw audio data samples of the first plane. This works
     * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
     * most audio decoders output planar audio, which uses a separate
     * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
     * In other words, this code will write only the first audio channel
     * in these cases.
     * You should use libswresample or libavfilter to convert the frame
     * to packed data. */
    fwrite(frame->extended_data[0], 1, unpadded_linesize, audio_dst_file);

    return 0;
}

int Capture::decode_packet(AVCodecContext* dec, const AVPacket* pkt)
{
    int ret = 0;

    // submit the packet to the decoder
    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0)
    {
        // std::cerr << "Error submitting a packet for decoding " << av_err2str(ret) << '\n'; // TODO
        return ret;
    }

    // get all the available frames from the decoder
    if (ret >= 0)
    {
        ret = avcodec_receive_frame(dec, frame);
        if (ret < 0)
        {
            // those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            // fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret)); // TODO
            return ret;
        }

        // write the frame data to output file
        // if (dec->codec->type == AVMEDIA_TYPE_VIDEO)
        //     ret = output_video_frame(frame);
        // else
        //     ret = output_audio_frame(frame);

        // av_frame_unref(frame);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int Capture::open_codec_context(int* stream_idx, AVCodecContext** dec_ctx, AVFormatContext* fmt_ctx, AVMediaType type)
{
    int            ret, stream_index;
    AVStream*      st;
    const AVCodec* dec = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0)
    {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n", av_get_media_type_string(type), src_filename);
        return ret;
    }
    else
    {
        stream_index = ret;
        st           = fmt_ctx->streams[stream_index];

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

Capture::Capture(std::filesystem::path const& path)
{
    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, path.string().c_str(), NULL, NULL) < 0)
    {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }
    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0)
    {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    int ret;
    if (open_codec_context(&video_stream_idx, &video_dec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0)
    {
        video_stream = fmt_ctx->streams[video_stream_idx];

        /* allocate image where the decoded image will be put */
        width   = video_dec_ctx->width;
        height  = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt; // TODO check that we support the pixel format. Or ask ffmpeg to convert to srgb?
        ret     = av_image_alloc(video_dst_data, video_dst_linesize, width, height, pix_fmt, 1);
        if (ret < 0)
        {
            fprintf(stderr, "Could not allocate raw video buffer\n");
            // goto end;
        }
        video_dst_bufsize = ret;
    }

    // TODO import audio too ?
    // if (open_codec_context(&audio_stream_idx, &audio_dec_ctx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0)
    // {
    //     audio_stream   = fmt_ctx->streams[audio_stream_idx];
    // }

    /* dump input information to stderr */
    // av_dump_format(fmt_ctx, 0, src_filename, 0);

    if (/* !audio_stream &&  */ !video_stream)
    {
        fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
        ret = 1;
        // goto end;
    }

    frame      = av_frame_alloc();
    rgba_frame = av_frame_alloc(); // TODO handle error
    if (!frame)
    {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        // goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt)
    {
        fprintf(stderr, "Could not allocate packet\n");
        ret = AVERROR(ENOMEM);
        // goto end;
    }
}

void Capture::move_to_next_frame()
{
    int ret;
    if (av_read_frame(fmt_ctx, pkt) >= 0)
    {
        // av_frame_unref(frame); // Delete previous frame
        // check if the packet belongs to a stream we are interested in, otherwise
        // skip it
        // TODO what does it mean ? Should we then try to read the frame after that one ? (NB: I think so, since a packet will only be video OR audio, every other packet is probably an audio packet)
        if (pkt->stream_index == video_stream_idx)
            ret = decode_packet(video_dec_ctx, pkt);
        else if (pkt->stream_index == audio_stream_idx)
            ret = decode_packet(audio_dec_ctx, pkt);
        av_packet_unref(pkt);
        // if (ret < 0) // TODO File end, handle this
        //     break;
    }
}

auto Capture::current_frame() const -> AVFrame const&
{
    if (frame->width != 0)
        convertFrameToRGBA(frame, rgba_frame); // TODO only convert if it doesn"t exist yet
    return *rgba_frame;
}

Capture::~Capture()
{
    /* flush the decoders */
    if (video_dec_ctx)
        decode_packet(video_dec_ctx, NULL);
    if (audio_dec_ctx)
        decode_packet(audio_dec_ctx, NULL);
    avcodec_free_context(&video_dec_ctx);
    avcodec_free_context(&audio_dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&rgba_frame);
    av_free(video_dst_data[0]);
}

} // namespace ffmpeg
