#include <iostream>
#include <vector>
#include <math.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavcodec/avcodec.h>
}

struct FrameData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb_pixels; // Packed RGB24 buffer
};

bool GrabSpecificFrame(const char* filename, int target_frame_number, FrameData& out_frame) {
    AVFormatContext* fmt_ctx = nullptr;
    av_log_set_level(AV_LOG_DEBUG);
    if (avformat_open_input(&fmt_ctx, filename, nullptr, nullptr) < 0) {
        std::cerr << "Could not open source file: " << filename << "\n";
        return false;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information\n";
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // Find the best video stream
    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx < 0) {
        std::cerr << "Could not find video stream\n";
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVCodecParameters* codec_par = fmt_ctx->streams[video_stream_idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codec_par->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(codec_ctx, codec_par);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec\n";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVStream* stream = fmt_ctx->streams[video_stream_idx];

    // Convert frame index to video timestamp (PTS)
    const double fps = av_q2d(stream->avg_frame_rate);
    const double target_time_sec = target_frame_number / fps;

    // Seek to nearest keyframe BEFORE target frame
    if (const auto target_pts = static_cast<int64_t>(target_time_sec / av_q2d(stream->time_base)); av_seek_frame(fmt_ctx, video_stream_idx, target_pts, AVSEEK_FLAG_BACKWARD) < 0) {
        std::cerr << "Error seeking to frame\n";
    }
    avcodec_flush_buffers(codec_ctx); // Clear decoder history after seek

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();

    // Prepare scaler for YUV -> RGB24 conversion
    SwsContext* sws_ctx = sws_getContext(
        codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
        codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    // Allocate buffer for RGB frame
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
    out_frame.rgb_pixels.resize(num_bytes);
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, out_frame.rgb_pixels.data(),
                         AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);

    bool frame_found = false;

    // Decode loop forward from the nearest keyframe to the target frame
    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_idx) {
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {

                    // Estimate current frame index using PTS
                    int current_frame = static_cast<int>(round(frame->pts * av_q2d(stream->time_base) * fps));

                    if (current_frame >= target_frame_number) {
                        // Convert YUV color space to packed RGB
                        sws_scale(sws_ctx, frame->data, frame->linesize, 0, codec_ctx->height,
                                  rgb_frame->data, rgb_frame->linesize);

                        out_frame.width = codec_ctx->width;
                        out_frame.height = codec_ctx->height;
                        frame_found = true;
                        break;
                    }
                }
            }
        }
        av_packet_unref(packet);
        if (frame_found) break;
    }

    // Cleanup memory
    sws_freeContext(sws_ctx);
    av_frame_free(&rgb_frame);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return frame_found;
}

int main() {
    FrameData frame;
    int target = 1; // 1st frame

    if (GrabSpecificFrame("test.webm", target, frame)) {
        std::cout << "Successfully grabbed frame " << target << "!\n";
        std::cout << "Dimensions: " << frame.width << "x" << frame.height << "\n";
        std::cout << "Buffer Size: " << frame.rgb_pixels.size() << " bytes\n";
        // frame.rgb_pixels now holds raw uint8_t RGB data ready for processing
    }

    return 0;
}