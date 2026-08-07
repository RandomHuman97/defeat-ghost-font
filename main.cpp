#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

struct FrameData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgbPixels = {}; // Packed RGB24 buffer
};

class VideoFrames {
private:
    int videoStreamIndex = -1;
    //int frameCounter;
    std::string filename;
    AVFormatContext *fmtCtx = nullptr;
    SwsContext *swsCtx = nullptr;
    AVCodecContext *codecCtx = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *rgbFrame = nullptr;

public:
    explicit VideoFrames(std::string filename) : filename(std::move(filename)) {
        if (avformat_open_input(&fmtCtx, this->filename.c_str(), nullptr, nullptr) < 0) {
            throw std::runtime_error("Could not open input file: " + this->filename);
        }
        if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
            throw std::runtime_error("Could not find stream info");
        }
        videoStreamIndex = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStreamIndex < 0) {
            throw std::runtime_error("Could not find a good stream :/");
        }
        //get best codec
        const AVCodecParameters *codec_par = fmtCtx->streams[videoStreamIndex]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
        if (!codec) {
            throw std::runtime_error("Could not find decoder");
        }
        codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx) {
            throw std::runtime_error("Could not allocate codec context");
        }

        if (avcodec_parameters_to_context(codecCtx, codec_par) < 0) {
            throw std::runtime_error("Could not copy codec parameters");
        }
        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            throw std::runtime_error("Could not open codec");
        }
        //allocate buffers
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        rgbFrame = av_frame_alloc();

        // sws context
        swsCtx = sws_getContext(
        codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
        codecCtx->width, codecCtx->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if (!packet || !frame || !rgbFrame || !swsCtx) {
            throw std::runtime_error("Could not allocate frame conversion state");
        }

    }

    ~VideoFrames() {
        sws_freeContext(swsCtx);
        av_frame_free(&rgbFrame);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
    }

    FrameData getNextFrame() const {
        FrameData frameData;
        while (av_read_frame(fmtCtx, packet) >= 0) {
            if (packet->stream_index != videoStreamIndex) {
                av_packet_unref(packet);
                continue;
            }

            int result = avcodec_send_packet(codecCtx, packet);
            av_packet_unref(packet);
            if (result < 0) {
                throw std::runtime_error("Could not send packet to decoder");
            }

            result = avcodec_receive_frame(codecCtx, frame);
            if (result == AVERROR(EAGAIN)) {
                continue;
            }
            if (result == AVERROR_EOF) {
                return {};
            }
            if (result < 0) {
                throw std::runtime_error("Could not receive decoded frame");
            }

            frameData.width = codecCtx->width;
            frameData.height = codecCtx->height;
            const int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frameData.width, frameData.height, 1);
            frameData.rgbPixels.resize(numBytes);

            av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, frameData.rgbPixels.data(), AV_PIX_FMT_RGB24, frameData.width, frameData.height, 1);
            sws_scale(swsCtx, frame->data, frame->linesize, 0, frameData.height,
                      rgbFrame->data, rgbFrame->linesize);

            return frameData;
        }

        return {};


    }
};

int main() {

    VideoFrames videoFrames("test.webm");

    std::cout << videoFrames.getNextFrame().rgbPixels[0];
    return 0;
}
