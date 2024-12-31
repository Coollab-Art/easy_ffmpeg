#include "VideoDecoder.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include "../include/easy_ffmpeg/callbacks.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace ffmpeg {

static auto fast_seeking_callback() -> std::function<void()>&
{
    static std::function<void()> instance = []() {
    };
    return instance;
}
static auto frame_decoding_error_callback() -> std::function<void(std::string const&)>&
{
    static std::function<void(std::string const&)> instance = [](std::string const&) {
    };
    return instance;
}

static auto fast_seeking_callback_mutex() -> std::mutex& // Need to lock since multiple VideoDecoders could each spawn a thread that would try to access this
{
    static std::mutex instance{};
    return instance;
}
static auto frame_decoding_error_callback_mutex() -> std::mutex& // Need to lock since multiple VideoDecoders could each spawn a thread that would try to access this
{
    static std::mutex instance{};
    return instance;
}

void set_fast_seeking_callback(std::function<void()> callback)
{
    std::unique_lock lock{fast_seeking_callback_mutex()};
    fast_seeking_callback() = std::move(callback);
}
void set_frame_decoding_error_callback(std::function<void(std::string const&)> callback)
{
    std::unique_lock lock{frame_decoding_error_callback_mutex()};
    frame_decoding_error_callback() = std::move(callback);
}

static auto format_error(std::string message, int err) -> std::string
{
    assert(err < 0);
    auto err_str_buffer = std::array<char, AV_ERROR_MAX_STRING_SIZE>{};
    av_strerror(err, err_str_buffer.data(), err_str_buffer.size());
    message += ":\n";
    message += err_str_buffer.data();
    return message;
}

static void throw_error(std::string const& message)
{
    throw std::runtime_error(message);
}

static void throw_error(std::string const& message, int err)
{
    throw_error(format_error(message, err));
}

void VideoDecoder::log_frame_decoding_error(std::string const& error_message)
{
    {
        std::unique_lock lock{frame_decoding_error_callback_mutex()};
        frame_decoding_error_callback()(error_message);
    }
    _error_count.fetch_add(1);
}

void VideoDecoder::log_frame_decoding_error(std::string const& error_message, int err)
{
    log_frame_decoding_error(format_error(error_message, err));
}

static auto tmp_string_for_detailed_info() -> std::string&
{
    thread_local auto instance = std::string{};
    return instance;
}

auto VideoDecoder::retrieve_detailed_info() const -> std::string
{
    tmp_string_for_detailed_info() = "";
    // We need to redirect ffmpeg's logging to our own string
    av_log_set_callback([](void*, int, const char* fmt, va_list vl) {
        va_list vl2; // NOLINT(*init-variables)
        va_copy(vl2, vl);
        auto const length = static_cast<size_t>(vsnprintf(nullptr, 0, fmt, vl));
        va_end(vl);
        std::vector<char> buffer(length + 1);
        vsnprintf(buffer.data(), length + 1, fmt, vl2); // NOLINT(cert-err33-c)
        va_end(vl2);
        tmp_string_for_detailed_info() += std::string{buffer.data()};
    });
    av_dump_format(_format_ctx, _video_stream_idx, "", false /*is_output*/);
    av_log_set_callback(&av_log_default_callback);
    return tmp_string_for_detailed_info();
}

VideoDecoder::VideoDecoder(std::filesystem::path const& path, AVPixelFormat pixel_format)
{
    {
        int const err = avformat_open_input(&_format_ctx, path.string().c_str(), nullptr, nullptr);
        if (err < 0)
            throw_error("Could not open file. Make sure the path is valid and is an actual video file", err);
    }
    {
        int const err = avformat_open_input(&_format_ctx_to_test_seeking, path.string().c_str(), nullptr, nullptr);
        if (err < 0)
            throw_error("Could not open file. Make sure the path is valid and is an actual video file", err);
    }

    {
        int const err = avformat_find_stream_info(_format_ctx, nullptr);
        if (err < 0)
            throw_error("Could not find stream information. Your file is most likely corrupted or not a valid video file", err);
    }
    {
        int const err = avformat_find_stream_info(_format_ctx_to_test_seeking, nullptr);
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

    _desired_color_space_frame = av_frame_alloc();
    _packet                    = av_packet_alloc();
    _packet_to_test_seeking    = av_packet_alloc();
    if (!_desired_color_space_frame || !_packet || !_packet_to_test_seeking)
        throw_error("Not enough memory to open the video file");

    _sws_ctx = sws_getContext(
        params.width, params.height,
        static_cast<AVPixelFormat>(params.format),
        params.width, params.height,
        pixel_format,
        0, nullptr, nullptr, nullptr
    );
    if (!_sws_ctx)
        throw_error("Failed to create conversion context");

    _desired_color_space_buffer = static_cast<uint8_t*>(av_malloc(sizeof(uint8_t) * static_cast<size_t>(av_image_get_buffer_size(pixel_format, params.width, params.height, 1))));
    if (!_desired_color_space_buffer)
        throw_error("Not enough memory to open the video file");

    {
        int const err = av_image_fill_arrays(_desired_color_space_frame->data, _desired_color_space_frame->linesize, _desired_color_space_buffer, pixel_format, params.width, params.height, 1);
        if (err < 0)
            throw_error("Failed to setup image arrays", err);
    }

    _detailed_info = retrieve_detailed_info();

    // Once the context is created, we can spawn the thread that will use this context and start decoding the frames
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
    // Must first stop the decoding thread, because it might be reading from the context, etc.
    _wants_to_stop_video_decoding_thread.store(true);
    _frames_queue.waiting_for_queue_to_empty_out().notify_all();
    _frames_queue.waiting_for_queue_to_fill_up().notify_all();
    _video_decoding_thread.join();

    if (_decoder_ctx)
        avcodec_send_packet(_decoder_ctx, nullptr); // Flush the decoder
    avcodec_free_context(&_decoder_ctx);
    avformat_close_input(&_format_ctx);
    avformat_close_input(&_format_ctx_to_test_seeking);
    av_packet_free(&_packet);
    av_packet_free(&_packet_to_test_seeking);

    av_frame_unref(_desired_color_space_frame);
    av_frame_free(&_desired_color_space_frame);
    av_free(_desired_color_space_buffer);
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
    {
        std::unique_lock lock{_mutex};
        for (AVFrame* frame : _alive_frames)
            _dead_frames.push_back(frame);
        _alive_frames.clear();
    }
    _waiting_for_pop.notify_one();
}

void VideoDecoder::video_decoding_thread_job(VideoDecoder& This)
{
    // TODO if thread has filled up the queue, it can start converting frames to RGBA in the meantime, instead of doing nothing
    while (!This._wants_to_stop_video_decoding_thread.load())
    {
        if (This._frames_queue.is_full() && This._seek_target.has_value() && This.present_time(This._frames_queue.second()) < *This._seek_target) // We are fast-seeking, don't wait for frames to be consumed, process new frames asap
            This._frames_queue.pop();

        // Pop from dead list
        std::unique_lock lock{This._decoding_context_mutex};
        This._frames_queue.waiting_for_queue_to_empty_out().wait(lock, [&] { return (!This._frames_queue.is_full() && !This._has_reached_end_of_file.load()) || This._wants_to_stop_video_decoding_thread.load() || This._wants_to_pause_decoding_thread_asap.load(); });
        if (This._wants_to_stop_video_decoding_thread.load()) // Thread has been woken up because it is getting destroyed, exit asap
            break;
        if (This._wants_to_pause_decoding_thread_asap.load())
            continue;

        AVFrame* const frame = This._frames_queue.get_frame_to_fill();

        if (This._wants_to_stop_video_decoding_thread.load())
            break;
        if (This._wants_to_pause_decoding_thread_asap.load())
            continue;

        // Decode frame
        bool const frame_is_valid = [&]() { // IIFE
            try
            {
                return This.decode_next_frame_into(frame);
            }
            catch (std::exception const& e)
            {
                This.log_frame_decoding_error(e.what());
                if (This.too_many_errors())
                    This._frames_queue.waiting_for_queue_to_fill_up().notify_one();
                return false;
            }
        }();

        if (This._wants_to_stop_video_decoding_thread.load())
            break;
        if (!frame_is_valid || This._wants_to_pause_decoding_thread_asap.load())
            continue;

        // Push to alive list
        This._frames_queue.push(frame);
        if (This._seek_target.has_value())
        {
            std::unique_lock lock2{fast_seeking_callback_mutex()};
            fast_seeking_callback()();
        }
    }
}

auto VideoDecoder::get_frame_at(double time_in_seconds, SeekMode seek_mode) -> std::optional<Frame>
{
    AVFrame const* frame_in_wrong_colorspace = get_frame_at_impl(time_in_seconds, seek_mode);
    if (!frame_in_wrong_colorspace)
        return std::nullopt;
    assert(frame_in_wrong_colorspace->width != 0 && frame_in_wrong_colorspace->height != 0);

    bool const is_different_from_previous_frame = frame_in_wrong_colorspace->pts != _previous_pts;
    _previous_pts                               = frame_in_wrong_colorspace->pts;
    if (is_different_from_previous_frame)
        convert_frame_to_desired_color_space(*frame_in_wrong_colorspace);
    return Frame{
        .data                             = _desired_color_space_frame->data[0],
        .width                            = frame_in_wrong_colorspace->width,
        .height                           = frame_in_wrong_colorspace->height,
        .is_different_from_previous_frame = is_different_from_previous_frame,
        .is_last_frame                    = _has_reached_end_of_file.load() && _frames_queue.size() == 1,
    };
}

auto VideoDecoder::present_time(AVFrame const& frame) const -> double
{
    return static_cast<double>(frame.pts) * av_q2d(video_stream().time_base);
}
auto VideoDecoder::present_time(AVPacket const& packet) const -> double
{
    return static_cast<double>(packet.pts) * av_q2d(video_stream().time_base);
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
    {
        auto const timestamp = static_cast<int64_t>(time_in_seconds / av_q2d(video_stream().time_base));

        int const err = avformat_seek_file(_format_ctx_to_test_seeking, _video_stream_idx, INT64_MIN, timestamp, timestamp, 0);
        if (err < 0)
            return false;
    }

    while (true)
    {
        PacketRaii packet_raii{_packet_to_test_seeking}; // Will unref the packet when exiting the scope

        { // Read data from the file and put it in the packet
            int const err = av_read_frame(_format_ctx_to_test_seeking, _packet_to_test_seeking);
            if (err == AVERROR_EOF)
                return false; // Shouldn't happen anyways (the first packet after seeking should never be after the end of the file). But if it does, this is probably not a keyframe we want to seek to.
            if (err < 0)
                return false;
        }

        // Check if the packet belongs to the video stream, otherwise skip it
        if (_packet_to_test_seeking->stream_index != _video_stream_idx)
            continue;

        {
            std::unique_lock lock{_frames_queue.mutex()};
            _frames_queue.waiting_for_queue_to_fill_up().wait(lock, [&]() { return _frames_queue.size_no_lock() >= 1 || _has_reached_end_of_file.load() || too_many_errors(); });
        }
        if (_frames_queue.is_empty()) // Can happen if there are errors while decoding frames, or if we reach the end of an empty file.
            return false;
        return present_time(*_packet_to_test_seeking) > present_time(_frames_queue.first());
    }
}

auto VideoDecoder::get_frame_at_impl(double time_in_seconds, SeekMode seek_mode) -> AVFrame const* // NOLINT(*cognitive-complexity)
{
    _error_count.store(0); // Reset error count. We stop if 5 errors occur while we wait for frames.

    time_in_seconds = std::clamp(time_in_seconds, 0., duration_in_seconds());
    bool const fast_mode{seek_mode == SeekMode::Fast};

    // We will return the first frame in the stream that has a present_time greater than time_in_seconds
    for (int attempt_count = 0;; ++attempt_count)
    {
        {
            std::unique_lock lock{_frames_queue.mutex()};
            _frames_queue.waiting_for_queue_to_fill_up().wait(lock, [&]() { return _frames_queue.size_no_lock() >= 2 || _has_reached_end_of_file.load() || too_many_errors(); });
        }
        if (_frames_queue.is_empty()) // Can happen if there are errors while decoding frames, or if we reach the end of an empty file.
            return nullptr;

        bool const should_seek = [&]() // IIFE
        {
            auto const current_time = _seek_target.value_or(present_time(_frames_queue.first()));

            // Seek backward
            if (time_in_seconds < current_time)
                return true;

            // No need to seek forward if we have reached end of file
            if (_has_reached_end_of_file.load())
                return false;

            // Seek forward more than 1 second
            if (attempt_count == 0 && current_time < time_in_seconds - 1.f && seeking_would_move_us_forward(time_in_seconds))
                return true;

            // If we have read quite a few frames and still not found the target time, we might be better off seeking
            if (attempt_count == 15 && seeking_would_move_us_forward(time_in_seconds)) // TODO do we keep it ? Do we change the number
                return true;

            return false;
        }();

        if (should_seek)
        {
            _wants_to_pause_decoding_thread_asap.store(true);
            _frames_queue.waiting_for_queue_to_empty_out().notify_one();
            std::unique_lock lock{_decoding_context_mutex}; // Lock the decoding thread at the beginning of its loop
            _wants_to_pause_decoding_thread_asap.store(false);

            auto const timestamp = static_cast<int64_t>(time_in_seconds / av_q2d(video_stream().time_base));
            int const  err       = avformat_seek_file(_format_ctx, _video_stream_idx, INT64_MIN, timestamp, timestamp, 0);
            if (err >= 0) // Failing to seek is not a problem, we will just continue without seeking
            {
                avcodec_flush_buffers(_decoder_ctx);
                _frames_queue.clear();
                _has_reached_end_of_file.store(false);
                if (fast_mode)
                    _seek_target = time_in_seconds;
                else
                    process_packets_until(time_in_seconds);
            }
        }

        if ((_has_reached_end_of_file.load() || too_many_errors()) && _frames_queue.size() == 1) //  Must be done after seeking, and after discarding all the frames that are past. Because if we are requested a time that is in the past, we need to seek, and can't just return early because we have reached the end of the file.
        {
            return &_frames_queue.first(); // Return the last frame that we decoded before reaching end of file, aka the last frame of the file
        }

        while (_frames_queue.size() >= 2)
        {
            if (present_time(_frames_queue.second()) > time_in_seconds) // We found the exact requested frame
            {
                _seek_target.reset();
                return &_frames_queue.first();
            }
            _frames_queue.pop(); // We want to see something that is past that frame, we can discard it now
        }

        // assert(_frames_queue.size() <= 1); // Wrong, decoding thread might have given us another frame in the meantime, after ending the while loop above. We should still use the first frame in the queue and not the last, since we don't know if the frames after the first one or above or below the time we seek
        if (fast_mode && !_frames_queue.is_empty())
            return &_frames_queue.first();
    }
}

void VideoDecoder::process_packets_until(double time_in_seconds) // NOLINT(*cognitive-complexity)
{
    assert(_frames_queue.is_empty());
    while (true)
    {
        if (too_many_errors())
            break; // Avoid beeing stuck here indefinitely if we cannot read any frames

        PacketRaii packet_raii{_packet}; // Will unref the packet when exiting the scope

        { // Read data from the file and put it in the packet
            int const err = av_read_frame(_format_ctx, _packet);
            if (err == AVERROR_EOF)
            {
                _has_reached_end_of_file.store(true);
                return;
            }
            if (err < 0)
            {
                log_frame_decoding_error("Failed to read video packet", err);
                continue;
            }
        }

        // Check if the packet belongs to the video stream, otherwise skip it
        if (_packet->stream_index != _video_stream_idx)
            continue;

        { // Send the packet to the decoder
            int const err = avcodec_send_packet(_decoder_ctx, _packet);
            assert(err != AVERROR_EOF);     // "the decoder has been flushed, and no new packets can be sent to it" Should never happen if we do our job properly
            assert(err != AVERROR(EINVAL)); // "codec not opened, it is an encoder, or requires flush" Should never happen if we do our job properly
            if (err < 0 && err != AVERROR(EAGAIN))
            {
                log_frame_decoding_error("Error submitting a video packet for decoding", err);
                continue;
            }
        }

        AVFrame* frame = _frames_queue.get_frame_to_fill();
        { // Read a frame from the packet that was sent to the decoder. For video streams a packet only contains one frame so there is no need to call avcodec_receive_frame() in a loop
            int const err = avcodec_receive_frame(_decoder_ctx, frame);
            if (err == AVERROR(EAGAIN)) // EAGAIN is a special error that is not a real problem, we just need to resend a packet
                continue;
            assert(err != AVERROR_EOF);     // "the codec has been fully flushed, and there will be no more output frames" Should never happen if we do our job properly
            assert(err != AVERROR(EINVAL)); // "codec not opened, or it is an encoder without the AV_CODEC_FLAG_RECON_FRAME flag enabled" Should never happen if we do our job properly
            if (err < 0)
            {
                log_frame_decoding_error("Error while decoding the video", err);
                continue;
            }
        }

        _frames_queue.push(frame);
        if (_frames_queue.size() > 2)
            _frames_queue.pop();

        if (present_time(*frame) > time_in_seconds && _frames_queue.size() > 1)
            break;
    }

    assert(_frames_queue.size() == 2 || too_many_errors());
}

void VideoDecoder::convert_frame_to_desired_color_space(AVFrame const& frame) const
{
    sws_scale(_sws_ctx, frame.data, frame.linesize, 0, frame.height, _desired_color_space_frame->data, _desired_color_space_frame->linesize);
}

auto VideoDecoder::decode_next_frame_into(AVFrame* frame) -> bool
{
    while (true)
    {
        PacketRaii packet_raii{_packet}; // Will unref the packet when exiting the scope

        { // Read data from the file and put it in the packet (most of the time this will be the actual video frame, but it can also be additional data, in which case avcodec_receive_frame() will return AVERROR(EAGAIN))
            int const err = av_read_frame(_format_ctx, _packet);
            if (err == AVERROR_EOF)
            {
                _has_reached_end_of_file.store(true);
                _frames_queue.waiting_for_queue_to_fill_up().notify_one();
                return false;
            }
            if (err < 0)
                throw_error("Failed to read video packet", err);
        }

        if (_wants_to_pause_decoding_thread_asap.load() || _wants_to_stop_video_decoding_thread.load())
            return false;

        // Check if the packet belongs to the video stream, otherwise skip it
        if (_packet->stream_index != _video_stream_idx)
            continue;

        { // Send the packet to the decoder
            int const err = avcodec_send_packet(_decoder_ctx, _packet);
            assert(err != AVERROR_EOF);     // "the decoder has been flushed, and no new packets can be sent to it" Should never happen if we do our job properly
            assert(err != AVERROR(EINVAL)); // "codec not opened, it is an encoder, or requires flush" Should never happen if we do our job properly
            if (err < 0 && err != AVERROR(EAGAIN))
                throw_error("Error submitting a video packet for decoding", err);
        }

        if (_wants_to_pause_decoding_thread_asap.load() || _wants_to_stop_video_decoding_thread.load())
            return false;

        { // Read a frame from the packet that was sent to the decoder. For video streams a packet only contains one frame so there is no need to call avcodec_receive_frame() in a loop
            int const err = avcodec_receive_frame(_decoder_ctx, frame);
            if (err == AVERROR(EAGAIN)) // EAGAIN is a special error that is not a real problem, we just need to resend a packet
                continue;
            assert(err != AVERROR_EOF);     // "the codec has been fully flushed, and there will be no more output frames" Should never happen if we do our job properly
            assert(err != AVERROR(EINVAL)); // "codec not opened, or it is an encoder without the AV_CODEC_FLAG_RECON_FRAME flag enabled" Should never happen if we do our job properly
            if (err < 0)
                throw_error("Error while decoding the video", err);
        }

        break; // Frame has been successfully read, we can stop the loop
    }

    return true;
}

auto VideoDecoder::video_stream() const -> AVStream const&
{
    return *_format_ctx->streams[_video_stream_idx]; // NOLINT(*pointer-arithmetic)
}

[[nodiscard]] auto VideoDecoder::duration_in_seconds() const -> double
{
    return static_cast<double>(_format_ctx->duration) / static_cast<double>(AV_TIME_BASE);
}

} // namespace ffmpeg
