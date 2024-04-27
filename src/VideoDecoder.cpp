#include "VideoDecoder.hpp"
#include <libavutil/frame.h>
#include <cassert>
#include <chrono> // TODO remove
#include <cstddef>
#include <iostream> // TODO remove
#include <stdexcept>
#include <thread>
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
    {
        int const err = avformat_open_input(&_format_ctx, path.string().c_str(), nullptr, nullptr);
        if (err < 0)
            throw_error("Could not open file. Make sure the path is valid and is an actual video file", err);
    }

    {
        int const err = avformat_find_stream_info(_format_ctx, nullptr);
        if (err < 0)
            throw_error("Could not find stream information. Your file is most likely corrupted or not a valid video file", err);
    }

    {
        int const err = av_find_best_stream(_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
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

    _decoder_ctx = avcodec_alloc_context3(decoder);
    if (!_decoder_ctx)
        throw_error("Not enough memory to open the video file");

    {
        int const err = avcodec_parameters_to_context(_decoder_ctx, &params);
        if (err < 0)
            throw_error("Failed to copy codec parameters to decoder context", err);
    }

    {
        int const err = avcodec_open2(_decoder_ctx, decoder, nullptr);
        if (err < 0)
        {
            auto const* desc = avcodec_descriptor_get(params.codec_id);
            throw_error("Failed to open codec \"" + std::string{desc ? desc->name : "Unknown"} + "\" (" + std::string{desc ? desc->long_name : "Unknown"} + ")", err);
        }
    }

    for (AVFrame*& frame : _frames)
    {
        frame = av_frame_alloc();
        if (!frame)
            throw_error("Not enough memory to open the video file");
    }
    _rgba_frame = av_frame_alloc();
    _packet     = av_packet_alloc();
    if (!_rgba_frame || !_packet)
        throw_error("Not enough memory to open the video file");

    // TODO convert to sRGB (I think AV_PIX_FMT_RGBA is linear rgb)
    _sws_ctx = sws_getContext(
        params.width, params.height,
        static_cast<AVPixelFormat>(params.format),
        params.width, params.height,
        AV_PIX_FMT_RGBA,
        0, nullptr, nullptr, nullptr
    );
    if (!_sws_ctx)
        throw_error("Failed to create RGBA conversion context");

    _rgba_buffer = static_cast<uint8_t*>(av_malloc(sizeof(uint8_t) * static_cast<size_t>(av_image_get_buffer_size(AV_PIX_FMT_RGBA, params.width, params.height, 1))));
    if (!_rgba_buffer)
        throw_error("Not enough memory to open the video file");

    {
        int const err = av_image_fill_arrays(_rgba_frame->data, _rgba_frame->linesize, _rgba_buffer, AV_PIX_FMT_RGBA, params.width, params.height, 1);
        if (err < 0)
            throw_error("Failed to setup image arrays", err);
    }

    for (size_t i = 0; i < _frames.size(); ++i)
        _dead_frames.push_back(i);

    // Once all the context is created, we can spawn the thread that will use this context and start decoding the frames
    _video_decoding_thread = std::thread{&VideoDecoder::video_decoder_thread_job, std::ref(*this)};
}

VideoDecoder::~VideoDecoder()
{
    _wants_to_stop_video_decoding_thread.store(true);
    _waiting_for_dead_frames_to_be_filled.notify_all();
    // TODO also need to notify the wait_condition, to make sure the thread is not  bloqued waiting. And when it wakes up, it needs to check if it needs to quit.
    _video_decoding_thread.join(); // Must be done first, because it might be reading from the context, etc.

    if (_decoder_ctx)
        avcodec_send_packet(_decoder_ctx, nullptr); // Flush the decoder
    avcodec_free_context(&_decoder_ctx);
    avformat_close_input(&_format_ctx);
    av_packet_free(&_packet);
    for (AVFrame* frame : _frames)
    {
        av_frame_unref(frame);
        av_frame_free(&frame);
    }
    av_frame_unref(_rgba_frame);
    av_frame_free(&_rgba_frame);
    av_free(_rgba_buffer);
    sws_freeContext(_sws_ctx);
}

// TODO stop processing frames once we have reached end of file (and only start again when we seek)
void VideoDecoder::video_decoder_thread_job(VideoDecoder& This)
{
    while (!This._wants_to_stop_video_decoding_thread.load())
    {
        // Pop from dead list
        size_t const frame_index = This.wait_for_dead_frame();
        if (This._wants_to_stop_video_decoding_thread.load()) // Thread has been woken up because it is getting destroyed, exit asap
            break;
        // Make frame valid
        // std::cout << "Decoding frame " << frame_index << '\n';
        This.decode_next_frame_into(This._frames[frame_index]); // TODO do we need to lock that frame?
        // Push to alive list
        This.mark_alive(frame_index);
    }
}

auto VideoDecoder::wait_for_dead_frame() -> size_t
{
    std::unique_lock lock{_dead_frames_mutex};
    _waiting_for_dead_frames_to_be_filled.wait(lock, [&] { return !_dead_frames.empty() || _wants_to_stop_video_decoding_thread.load(); });
    if (_wants_to_stop_video_decoding_thread.load()) // Thread has been woken up because it is getting destroyed, exit asap
        return 0;

    assert(!_dead_frames.empty());
    auto const res = _dead_frames.back();
    _dead_frames.pop_back();
    return res;
}

void VideoDecoder::mark_alive(size_t frame_index)
{
    std::unique_lock lock{_alive_frames_mutex};
    _alive_frames.push_back(frame_index);
}

void VideoDecoder::mark_dead(size_t frame_index)
{
    std::unique_lock lock{_dead_frames_mutex};
    _dead_frames.push_back(frame_index);
    _alive_frames.erase(std::remove(_alive_frames.begin(), _alive_frames.end(), frame_index));
    _waiting_for_dead_frames_to_be_filled.notify_one();
}

auto VideoDecoder::get_frame_at(double time_in_seconds) -> AVFrame const&
{
    // assert(_frame->width != 0 && _frame->height != 0); // TODO handle calls of current_frame() when end of file has been reached
    // TODO move the conversion to the thread too ? What is better for performance ? (nb: there might be different scenarios : normal playback, fast forwarding, playing backwards etc.)
    convert_frame_to_rgba(get_frame_at_impl(time_in_seconds)); // TODO only convert if it doesn"t exist yet // TODO add param to choose color spae, and store a map of all frames in all color spaces that have been requested
    return *_rgba_frame;
}

auto VideoDecoder::present_time(AVFrame const& frame) const -> double
{
    return (double)frame.pts * (double)video_stream().time_base.num / (double)video_stream().time_base.den;
}

auto VideoDecoder::get_frame_at_impl(double time_in_seconds) -> AVFrame const&
{
    // We will return the first frame in the stream that has a present_time greater than time_in_seconds

    // We want to see something that is before the first frame available, we need to seek
    // if (time_in_seconds < first_frame().present_time())
    //     return seek_backward();

    // TODO if we see that the time_in_seconds is far after the time of the last available frame, seek immediately instead of checking frames, freeing them and letting the thread read the next frames
    while (_alive_frames.size() < 2) // TODO remove, and instead use a wait condition to wait for frames to be decoded
        std::this_thread::sleep_for(std::chrono::milliseconds{10});

    std::unique_lock lock{_alive_frames_mutex}; // TODO could be a shared_lock, but is it any good ?
    // std::cout << "Showing frame " << _alive_frames[1] << '\n';
    for (size_t i = 1; i < _alive_frames.size(); ++i)
    {
        if (present_time(*_frames[_alive_frames[i]]) > time_in_seconds) // get_frame(i) might need to wait if the thread hasn't produced that frame yet
            return *_frames[_alive_frames[i - 1]];
        mark_dead(_alive_frames[i - 1]); // We want to see something that is past that frame, we can discard it now
    }
    return *_frames[_alive_frames.back()]; // TODO remove and use seek_forward() instead

    // We want to see something that is after the last frame available, we need to seek
    // return seek_forward();
}

void VideoDecoder::convert_frame_to_rgba(AVFrame const& frame) const
{
    // TODO check, if this frame is already the one we converted last time, then there is nothing to do (we probably have a frame id / number, or at least we can use its presentation timestamp)
    sws_scale(_sws_ctx, frame.data, frame.linesize, 0, frame.height, _rgba_frame->data, _rgba_frame->linesize);
    _rgba_frame->width  = frame.width;
    _rgba_frame->height = frame.height;
    _rgba_frame->format = frame.format;
    _rgba_frame->pts    = frame.pts; // TODO keep this ? Create our own Frame struct ?
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

auto VideoDecoder::decode_next_frame_into(AVFrame* frame) -> bool
{
    // av_frame_unref(_frame); // Delete previous frame // TODO might not be needed, because avcodec_receive_frame() already calls av_frame_unref at the beginning

    while (true)
    {
        PacketRaii packet_raii{_packet}; // Will unref the packet when exiting the scope

        { // Reads data from the file and puts it in the packet (most of the time this will be the actual video frame, but it can also be additional data, in which case avcodec_receive_frame() will return AVERROR(EAGAIN))
            int const err = av_read_frame(_format_ctx, _packet);
            if (err == AVERROR_EOF)
                return false;
            if (err < 0)
                throw_error("Failed to read video packet", err);
        }

        // Check if the packet belongs to the video stream, otherwise skip it
        if (_packet->stream_index != _video_stream_idx)
            continue;

        { // Send the packet to the decoder
            int const err = avcodec_send_packet(_decoder_ctx, _packet);
            // TODO actually the docs seems to say that we must continue and call avcodec_receive_frame()
            // if (err == AVERROR(EAGAIN)) // "input is not accepted in the current state - user must read output with avcodec_receive_frame()" Should never happen for video packets, they always contain only one frame
            //     continue;
            assert(err != AVERROR_EOF);     // "the decoder has been flushed, and no new packets can be sent to it" Should never happen if we do our job properly
            assert(err != AVERROR(EINVAL)); // "codec not opened, it is an encoder, or requires flush" Should never happen if we do our job properly
            if (err < 0 && err != AVERROR(EAGAIN))
                throw_error("Error submitting a video packet for decoding", err);
        }

        { // Read a frame from the packet that was sent to the decoder. For video streams a packet only contains one frame so no need to call avcodec_receive_frame() in a loop
            int const err = avcodec_receive_frame(_decoder_ctx, frame);
            if (err == AVERROR(EAGAIN)) // EAGAIN is a special error that is not a real problem, we just need to resend a packet
                continue;
            // TODO only receive the frame when current_frame() is called ? But how do we handle EAGAIN then?
            // assert(err != AVERROR(EAGAIN)); // Actually this can happen, if the frame in the packet is not a video frame, but just some extra information
            assert(err != AVERROR_EOF);            // "the codec has been fully flushed, and there will be no more output frames" Should never happen if we do our job properly
            assert(err != AVERROR(EINVAL));        // "codec not opened, or it is an encoder without the AV_CODEC_FLAG_RECON_FRAME flag enabled" Should never happen if we do our job properly
            if (err < 0 && err != AVERROR(EAGAIN)) // EAGAIN is a special error that is not a real problem, we just need to resend a packet
                throw_error("Error while decoding the video", err);
        }

        break; // Frame has been successfully read, we can stop the loop
    }

    return true;
}

// void VideoDecoder::seek_to(int64_t time_in_nanoseconds)
// {
//     // TODO video_stream().start_time
//     auto const timestamp = time_in_nanoseconds * video_stream().time_base.den / video_stream().time_base.num / 1'000'000'000;
//     // std::cout << timestamp << ' ' << video_stream().time_base.num << ' ' << video_stream().time_base.den << '\n';
//     av_seek_frame(_format_ctx, _video_stream_idx, timestamp, 0);
//     // move_to_next_frame();
//     // int const err = avformat_seek_file(_format_ctx, _video_stream_idx, timestamp, timestamp, INT64_MAX, 0);
//     avcodec_flush_buffers(_decoder_ctx);
//     // if (err < 0)
//     //     throw_error("Failed to seek to " + std::to_string(time_in_nanoseconds) + " nanoseconds", err);
//     bool found = false;
//     while (!found)
//     {
//         PacketRaii packet_raii{_packet}; // Will unref the packet when exiting the scope

//         { // Reads data from the file and puts it in the packet (most of the time this will be the actual video frame, but it can also be additional data, in which case avcodec_receive_frame() will return AVERROR(EAGAIN))
//             assert(_packet != nullptr);
//             int const err = av_read_frame(_format_ctx, _packet);
//             if (err == AVERROR_EOF)
//             {
//                 // seek_to_start();
//                 // continue;
//                 throw_error("Should never happen");
//             }
//             if (err < 0)
//                 throw_error("Failed to read video packet", err);
//         }

//         // Check if the packet belongs to the video stream, otherwise skip it
//         if (_packet->stream_index != _video_stream_idx)
//             continue;

//         { // Send the packet to the decoder
//             int const err = avcodec_send_packet(_decoder_ctx, _packet);
//             // assert(err != AVERROR(EAGAIN)); // "input is not accepted in the current state - user must read output with avcodec_receive_frame()" Should never happen for video packets, they always contain only one frame
//             assert(err != AVERROR_EOF);     // "the decoder has been flushed, and no new packets can be sent to it" Should never happen if we do our job properly
//             assert(err != AVERROR(EINVAL)); // "codec not opened, it is an encoder, or requires flush" Should never happen if we do our job properly
//             if (err == AVERROR(EAGAIN))
//                 continue;
//             if (err < 0)
//                 throw_error("Error submitting a video packet for decoding", err);
//         }
//         auto const MyPts = av_rescale(_packet->pts, AV_TIME_BASE * (int64_t)video_stream().time_base.num, video_stream().time_base.den);
//         found            = MyPts > time_in_nanoseconds * AV_TIME_BASE / 1'000'000'000;

//         { // Read a frame from the packet that was sent to the decoder. For video streams a packet only contains one frame so no need to call avcodec_receive_frame() in a loop
//             int const err = avcodec_receive_frame(_decoder_ctx, _frame);
//             // assert(err != AVERROR(EAGAIN)); // Actually this can happen, if the frame in the packet is not a video frame, but just some extra information
//             assert(err != AVERROR_EOF);            // "the codec has been fully flushed, and there will be no more output frames" Should never happen if we do our job properly
//             assert(err != AVERROR(EINVAL));        // "codec not opened, or it is an encoder without the AV_CODEC_FLAG_RECON_FRAME flag enabled" Should never happen if we do our job properly
//             if (err < 0 && err != AVERROR(EAGAIN)) // EAGAIN is a special error that is not a real problem, we just need to resend a packet
//                 throw_error("Error while decoding the video", err);

//             found = err != AVERROR(EAGAIN);
//         }
//     }

//     // move_to_next_frame();
//     // avcodec_flush_buffers(_decoder_ctx);
// }

// void VideoDecoder::seek_to_start()
// {
//     int const err = avformat_seek_file(_format_ctx, _video_stream_idx, 0, 0, 0, 0);
//     if (err < 0)
//         throw_error("Failed to seek to the start", err);
//     avcodec_flush_buffers(_decoder_ctx);
// }

auto VideoDecoder::video_stream() const -> AVStream const&
{
    return *_format_ctx->streams[_video_stream_idx]; // NOLINT(*pointer-arithmetic)
}

// TODO remove ?
[[nodiscard]] auto VideoDecoder::fps() const -> double
{
    //  TODO compute it only once in the constructor and then cache it
    return av_q2d(video_stream().avg_frame_rate); // TODO if avg_frame_rate is not set, then try r_frame_rate ?
}

// TODO use duration instead of frames_count ? _format_ctx->duration

// TODO remove ?
[[nodiscard]] auto VideoDecoder::frames_count() const -> int64_t
{
    auto const count = video_stream().nb_frames;
    if (count != 0)
        return count;

    // nb_frames is not set or accurate, calculate total frames from duration and framerate
    assert(false); // TODO remove, i just want to see if this procs sometimes
    AVRational const frameRate = video_stream().avg_frame_rate;
    int64_t const    duration  = _format_ctx->duration / AV_TIME_BASE;
    return av_rescale(duration, frameRate.num, frameRate.den);
}

} // namespace ffmpeg
