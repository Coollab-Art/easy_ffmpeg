#include "VideoDecoder.hpp"
#include <cassert>
#include <chrono>
#include <cstddef>
#include <iostream> // TODO remove
#include <mutex>
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

static auto fast_seeking_callback() -> std::function<void()>&
{
    static std::function<void()> instance = []() {
    };
    return instance;
}

void set_fast_seeking_callback(std::function<void()> callback)
{
    fast_seeking_callback() = std::move(callback);
}

VideoDecoder::VideoDecoder(std::filesystem::path const& path)
{
    {
        int const err = avformat_open_input(&_format_ctx, path.string().c_str(), nullptr, nullptr);
        if (err < 0)
            throw_error("Could not open file. Make sure the path is valid and is an actual video file", err);
    }
    {
        int const err = avformat_open_input(&_test_seek_format_ctx, path.string().c_str(), nullptr, nullptr);
        if (err < 0)
            throw_error("Could not open file. Make sure the path is valid and is an actual video file", err);
    }

    {
        int const err = avformat_find_stream_info(_format_ctx, nullptr);
        if (err < 0)
            throw_error("Could not find stream information. Your file is most likely corrupted or not a valid video file", err);
    }
    {
        int const err = avformat_find_stream_info(_test_seek_format_ctx, nullptr);
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

    _rgba_frame       = av_frame_alloc();
    _packet           = av_packet_alloc();
    _test_seek_packet = av_packet_alloc();
    if (!_rgba_frame || !_packet || !_test_seek_packet)
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

    // Once all the context is created, we can spawn the thread that will use this context and start decoding the frames
    _video_decoding_thread = std::thread{&VideoDecoder::video_decoding_thread_job, std::ref(*this)};
}

VideoDecoder::FramesQueue::FramesQueue()
{
    _dead_frames.resize(6);
    for (AVFrame*& frame : _dead_frames)
    {
        frame = av_frame_alloc();
        if (!frame)
            throw_error("Not enough memory to open the video file");
    }
}

VideoDecoder::~VideoDecoder()
{
    _wants_to_stop_video_decoding_thread.store(true);
    _frames_queue.waiting_for_queue_to_empty_out().notify_all();
    _frames_queue.waiting_for_queue_to_fill_up().notify_all();
    _video_decoding_thread.join(); // Must be done first, because it might be reading from the context, etc.

    if (_decoder_ctx)
        avcodec_send_packet(_decoder_ctx, nullptr); // Flush the decoder
    avcodec_free_context(&_decoder_ctx);
    avformat_close_input(&_format_ctx);
    avformat_close_input(&_test_seek_format_ctx);
    av_packet_free(&_packet);
    av_packet_free(&_test_seek_packet);

    av_frame_unref(_rgba_frame);
    av_frame_free(&_rgba_frame);
    av_free(_rgba_buffer);
    sws_freeContext(_sws_ctx);
}

VideoDecoder::FramesQueue::~FramesQueue()
{
    for (AVFrame*& frame : _alive_frames)
    {
        av_frame_unref(frame);
        av_frame_free(&frame);
    }
    for (AVFrame*& frame : _dead_frames)
    {
        av_frame_unref(frame);
        av_frame_free(&frame);
    }
}

auto VideoDecoder::FramesQueue::size() -> size_t
{
    std::unique_lock lock{_mutex};
    return size_no_lock();
}

auto VideoDecoder::FramesQueue::size_no_lock() -> size_t
{
    return _alive_frames.size();
}

auto VideoDecoder::FramesQueue::is_full() -> bool
{
    std::unique_lock lock{_mutex};
    return _dead_frames.empty();
}

auto VideoDecoder::FramesQueue::is_empty() -> bool
{
    std::unique_lock lock{_mutex};
    return _alive_frames.empty();
}

auto VideoDecoder::FramesQueue::first() -> AVFrame const&
{
    std::unique_lock lock{_mutex};
    return *_alive_frames[0];
}

auto VideoDecoder::FramesQueue::second() -> AVFrame const&
{
    std::unique_lock lock{_mutex};
    return *_alive_frames[1];
}

// auto VideoDecoder::FramesQueue::last() -> AVFrame const&
// {
//     std::unique_lock lock{_mutex};
//     return *_alive_frames.back();
// }

auto VideoDecoder::FramesQueue::get_frame_to_fill() -> AVFrame*
{
    std::unique_lock lock{_mutex};
    assert(!_dead_frames.empty());
    return _dead_frames[0];
}

void VideoDecoder::FramesQueue::push(AVFrame* frame)
{
    {
        std::unique_lock lock{_mutex};
        _alive_frames.push_back(frame);
        _dead_frames.erase(std::remove(_dead_frames.begin(), _dead_frames.end(), frame)); // NOLINT(*inaccurate-erase)
    }
    _waiting_for_push.notify_one();
}

static auto pop_front(std::vector<AVFrame*>& v) -> AVFrame*
{
    AVFrame* ret = v.front();
    v.erase(v.begin());
    return ret;
}

void VideoDecoder::FramesQueue::pop()
{
    {
        std::unique_lock lock{_mutex};
        _dead_frames.push_back(pop_front(_alive_frames));
    }
    _waiting_for_pop.notify_one();
}

void VideoDecoder::FramesQueue::clear()
{
    std::unique_lock lock{_mutex};
    for (AVFrame* frame : _alive_frames)
        _dead_frames.push_back(frame);
    _alive_frames.clear();
    _waiting_for_pop.notify_one();
}

// TODO stop processing frames once we have reached end of file (and only start again when we seek)
void VideoDecoder::video_decoding_thread_job(VideoDecoder& This)
{
    while (!This._wants_to_stop_video_decoding_thread.load())
    {
        if (This._frames_queue.is_full() && This._seek_target.has_value() && This.present_time(This._frames_queue.second()) < *This._seek_target)
            This._frames_queue.pop();
        // TODO if thread has filled up the queue, it can start converting frames to RGBA in the meantime, instead of doing nothing
        // Pop from dead list
        std::unique_lock lock{This._decoding_context_mutex};
        This._frames_queue.waiting_for_queue_to_empty_out().wait(lock, [&] { return (!This._frames_queue.is_full() && !This._has_reached_end_of_file.load()) || This._wants_to_stop_video_decoding_thread.load(); });
        if (This._wants_to_stop_video_decoding_thread.load()) // Thread has been woken up because it is getting destroyed, exit asap
            break;
        if (This._wants_to_pause_decoding_thread_asap.load())
            continue;

        AVFrame* frame = This._frames_queue.get_frame_to_fill();

        if (This._wants_to_pause_decoding_thread_asap.load())
            continue;

        // Make frame valid
        bool const frame_is_valid = This.decode_next_frame_into(frame);

        if (!frame_is_valid || This._wants_to_pause_decoding_thread_asap.load())
            continue;
        // Push to alive list
        This._frames_queue.push(frame);
        if (This._seek_target.has_value())
            fast_seeking_callback()();
    }
}

auto VideoDecoder::get_frame_at(double time_in_seconds, SeekMode seek_mode) -> Frame
{
    // TODO move the conversion to the thread too ? What is better for performance ? (nb: there might be different scenarios : normal playback, fast forwarding, playing backwards etc.)
    AVFrame const& frame_in_wrong_colorspace = get_frame_at_impl(time_in_seconds, seek_mode);
    assert(frame_in_wrong_colorspace.width != 0 && frame_in_wrong_colorspace.height != 0);

    bool const is_different_from_previous_frame = frame_in_wrong_colorspace.pts != _previous_pts;
    _previous_pts                               = frame_in_wrong_colorspace.pts;
    if (is_different_from_previous_frame)
        convert_frame_to_rgba(frame_in_wrong_colorspace); // TODO add param to choose color spae, and store a map of all frames in all color spaces that have been requested. Or just support one color space, chosen as a parameter to the constructor
    return Frame{
        .data                             = _rgba_frame->data[0],
        .width                            = frame_in_wrong_colorspace.width,
        .height                           = frame_in_wrong_colorspace.height,
        .color_channels_count             = 4,
        .is_different_from_previous_frame = is_different_from_previous_frame,
        .is_last_frame =
            _has_reached_end_of_file.load()
            && _frames_queue.size() == 1,
    };
}

auto VideoDecoder::present_time(AVFrame const& frame) const -> double
{
    return (double)frame.pts * (double)video_stream().time_base.num / (double)video_stream().time_base.den;
}
auto VideoDecoder::present_time(AVPacket const& packet) const -> double
{
    return (double)packet.pts * (double)video_stream().time_base.num / (double)video_stream().time_base.den;
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

auto VideoDecoder::seeking_would_move_us_forward(double time_in_seconds) -> bool
{
    auto const timestamp = time_in_seconds * AV_TIME_BASE;
    int const  err       = avformat_seek_file(_test_seek_format_ctx, -1, INT64_MIN, timestamp, timestamp, 0);
    while (true)
    {
        PacketRaii packet_raii{_test_seek_packet}; // Will unref the packet when exiting the scope

        { // Reads data from the file and puts it in the packet (most of the time this will be the actual video frame, but it can also be additional data, in which case avcodec_receive_frame() will return AVERROR(EAGAIN))
            int const err = av_read_frame(_test_seek_format_ctx, _test_seek_packet);
            // if (err == AVERROR_EOF) // TODO
            // {
            //     _has_reached_end_of_file.store(true);                      // TODO test what happens when we do a seek past the end of the file
            //     _frames_queue.waiting_for_queue_to_fill_up().notify_one(); // TODO not needed
            //     return;
            // }
            if (err < 0)
                throw_error("Failed to read video packet", err); // TODO doesn't throwing mess up our state?
        }

        // Check if the packet belongs to the video stream, otherwise skip it
        if (_test_seek_packet->stream_index != _video_stream_idx)
            continue;

        return present_time(*_test_seek_packet) > present_time(_frames_queue.first());
    }
}

auto VideoDecoder::get_frame_at_impl(double time_in_seconds, SeekMode seek_mode) -> AVFrame const&
{
    if (time_in_seconds < 0.)
        time_in_seconds = 0.;
    // time_in_seconds = std::clamp(time_in_seconds, 0., max_duration()); // TODO (and test, it should avoid flicker when seeking past the end)
    bool const fast_mode{seek_mode == SeekMode::Fast};
    // We will return the first frame in the stream that has a present_time greater than time_in_seconds

    // TODO maybe we should start seeking forward as soon as we have exhausted all the frames that were already decoded ? This is a good indication that the decoding thread cannot keep up with the playback speed
    for (int a = 0;; ++a)
    {
        if (a > 0)
            std::cout << a << '\n';

        {
            std::unique_lock lock{_frames_queue.mutex()};
            _frames_queue.waiting_for_queue_to_fill_up().wait(lock, [&]() { return _frames_queue.size_no_lock() >= 2 || _has_reached_end_of_file.load(); });
        }
        bool const should_seek = [&]() // IIFE
        {
            auto const bob = _seek_target.value_or(present_time(_frames_queue.first()));

            if (bob > time_in_seconds)
                return true; // Seek backward

            if (_frames_queue.is_empty()) // TODO this will never be empty, we need to check iif size <= 1 TODO is this a good idea ? An empty frames_queue indicates that none of the frames that were made ready by the decoding thread were at the right pts, and we need to decode new frames, so might as well seek
                return true;

            if (a == 15) // TODO and that ?
                return true;

            if (a == 0 && ((bob < time_in_seconds - 1.f && !_has_reached_end_of_file.load() && seeking_would_move_us_forward(time_in_seconds)))) // Seek forward more than 1 second // TODO check seeking_would_move_us_forward() in more cases?
                return true;

            return false;
        }();
        if (should_seek)
        {
            _wants_to_pause_decoding_thread_asap.store(true);
            std::unique_lock lock{_decoding_context_mutex}; // Lock the decoding thread at the beginning of its loop
            _wants_to_pause_decoding_thread_asap.store(false);
            // _wants_to_stop_video_decoding_thread.store(true);
            // _waiting_for_queue_to_not_be_full.notify_all();
            // _waiting_for_alive_frames_to_be_filled.notify_all();
            // // TODO also need to notify the wait_condition, to make sure the thread is not blocked waiting. And when it wakes up, it needs to check if it needs to quit.
            // _video_decoding_thread.join(); // Must be done first, because it might be reading from the context, etc.

            // std::unique_lock lock2{_decoding_context_mutex};
            auto const timestamp = time_in_seconds * AV_TIME_BASE; // video_stream().time_base.den / video_stream().time_base.num; // TODO use stream units
            // std::cout << timestamp << ' ' << video_stream().time_base.num << ' ' << video_stream().time_base.den << '\n';
            // av_seek_frame(_format_ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
            // move_to_next_frame();
            int const err = avformat_seek_file(_format_ctx, -1, INT64_MIN, timestamp, timestamp, 0);
            avcodec_flush_buffers(_decoder_ctx);
            _frames_queue.clear();
            _has_reached_end_of_file.store(false);
            if (!fast_mode) // TODO in fast mode we won't process, so _frames_queue will be empty after that and we will crash
                process_packets_until(time_in_seconds);
            else
                _seek_target = time_in_seconds;
            // TODO after seeking, set a "fast forward" mode on the decoding thread where it can skip the avcodec_receive_frame, until it reaches the right timestamp. This should be possible because packet has a pts, ne need to retrieve frame to get the pts

            // _waiting_for_queue_to_not_be_full.notify_one();
        }
        // _waiting_for_alive_frames_to_be_filled.wait(lock, [&]() { return _alive_frames.size() >= 1; });
        // else
        // {

        if (_has_reached_end_of_file.load() && _frames_queue.size() == 1) // TODO is this comment still true ? : Must be done after seeking, and after discarding all the frames that are past
        {
            return _frames_queue.first(); // Return the last frame that we decoded before reaching end of file, aka the last frame of the file
        }
        {
            assert(_frames_queue.size() >= 2 || fast_mode);
            while (_frames_queue.size() >= 2)
            {
                if (present_time(_frames_queue.second()) > time_in_seconds) // get_frame(i) might need to wait if the thread hasn't produced that frame yet
                {
                    _seek_target.reset();
                    return _frames_queue.first();
                }
                _frames_queue.pop(); // We want to see something that is past that frame, we can discard it now
            }

            // assert(_frames_queue.size() <= 1); // Wrong, decoding thread might have given us another frame in the meantime, after ending the while loop above. We should still use the first frame in the queue and not the last, since we don't know if the frames after the first one or above or below the time we seek
            if (fast_mode && !_frames_queue.is_empty())
                return _frames_queue.first();
        }
        // if (fast_mode) // TODO fast mode, when do we decide to stop testing all frames, and just return the current one
        //     return *_frames[copy.back()];
        // }
    }
}

void VideoDecoder::process_packets_until(double time_in_seconds)
{
    assert(_alive_frames.empty());
    while (true)
    {
        PacketRaii packet_raii{_packet}; // Will unref the packet when exiting the scope

        { // Reads data from the file and puts it in the packet (most of the time this will be the actual video frame, but it can also be additional data, in which case avcodec_receive_frame() will return AVERROR(EAGAIN))
            int const err = av_read_frame(_format_ctx, _packet);
            if (err == AVERROR_EOF)
            {
                _has_reached_end_of_file.store(true);                      // TODO test what happens when we do a seek past the end of the file
                _frames_queue.waiting_for_queue_to_fill_up().notify_one(); // TODO not needed
                return;
            }
            if (err < 0)
                throw_error("Failed to read video packet", err); // TODO doesn't throwing mess up our state?
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

        AVFrame* frame = _frames_queue.get_frame_to_fill();
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

        // std::cout << (present_time(*_frames[curr_frame]) - present_time(*_packet)) << '\n';
        _frames_queue.push(frame);
        if (_frames_queue.size() > 2)
            _frames_queue.pop();

        if (present_time(*frame) > time_in_seconds && _frames_queue.size() > 1)
            break;
    }

    assert(_frames_queue.size() == 2);
}

void VideoDecoder::convert_frame_to_rgba(AVFrame const& frame) const
{
    sws_scale(_sws_ctx, frame.data, frame.linesize, 0, frame.height, _rgba_frame->data, _rgba_frame->linesize);
}

auto VideoDecoder::decode_next_frame_into(AVFrame* frame) -> bool
{
    // std::unique_lock lock{_decoding_context_mutex};
    // av_frame_unref(_frame); // Delete previous frame // TODO might not be needed, because avcodec_receive_frame() already calls av_frame_unref at the beginning

    while (true)
    {
        PacketRaii packet_raii{_packet}; // Will unref the packet when exiting the scope

        { // Reads data from the file and puts it in the packet (most of the time this will be the actual video frame, but it can also be additional data, in which case avcodec_receive_frame() will return AVERROR(EAGAIN))
            int const err = av_read_frame(_format_ctx, _packet);
            if (err == AVERROR_EOF)
            {
                _has_reached_end_of_file.store(true);
                _frames_queue.waiting_for_queue_to_fill_up().notify_one();
                return false;
            }
            if (err < 0)
                throw_error("Failed to read video packet", err); // TODO what happens when we throw ? How does the decoding thread handle that ? Doesn't it mess up the state of the queue?
        }

        if (_wants_to_pause_decoding_thread_asap.load())
            return false;

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
