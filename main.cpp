#include <iostream>
#include <fstream>
#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>
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

static inline uint8_t similarity(const uint8_t n1, const uint8_t n2) {
    return std::numeric_limits<uint8_t>::max()-abs(n1-n2);
}

static void writePpm(const std::string& filename, const int width, const int height, const std::vector<uint8_t>& pixels) {
    std::ofstream output(filename, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Could not open output file: " + filename);
    }

    output << "P6\n" << width << " " << height << "\n255\n";
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
}

static std::vector<uint8_t> toGray(const FrameData& frame) {
    std::vector<uint8_t> gray(static_cast<size_t>(frame.width) * frame.height);
    for (size_t pixel = 0, rgb = 0; pixel < gray.size(); ++pixel, rgb += 3) {
        gray[pixel] = static_cast<uint8_t>((static_cast<int>(frame.rgbPixels[rgb]) +
            2 * static_cast<int>(frame.rgbPixels[rgb + 1]) +
            static_cast<int>(frame.rgbPixels[rgb + 2])) >> 2);
    }
    return gray;
}

static std::vector<uint8_t> makeBlurredPpm(const std::vector<int32_t>& values, const int width, const int height, const int radius) {
    std::vector<int32_t> integral(static_cast<size_t>(width + 1) * (height + 1), 0);
    for (int y = 0; y < height; ++y) {
        int32_t rowSum = 0;
        for (int x = 0; x < width; ++x) {
            rowSum += values[static_cast<size_t>(y) * width + x];
            integral[static_cast<size_t>(y + 1) * (width + 1) + x + 1] =
                integral[static_cast<size_t>(y) * (width + 1) + x + 1] + rowSum;
        }
    }

    int32_t maxValue = 1;
    std::vector<int32_t> blurred(values.size(), 0);
    for (int y = 0; y < height; ++y) {
        const int y0 = std::max(0, y - radius);
        const int y1 = std::min(height - 1, y + radius);
        for (int x = 0; x < width; ++x) {
            const int x0 = std::max(0, x - radius);
            const int x1 = std::min(width - 1, x + radius);
            const int area = (x1 - x0 + 1) * (y1 - y0 + 1);
            const int32_t sum = integral[static_cast<size_t>(y1 + 1) * (width + 1) + x1 + 1]
                - integral[static_cast<size_t>(y0) * (width + 1) + x1 + 1]
                - integral[static_cast<size_t>(y1 + 1) * (width + 1) + x0]
                + integral[static_cast<size_t>(y0) * (width + 1) + x0];
            const int32_t value = sum / area;
            blurred[static_cast<size_t>(y) * width + x] = value;
            maxValue = std::max(maxValue, value);
        }
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3, 0);
    for (size_t i = 0; i < blurred.size(); ++i) {
        const auto value = static_cast<uint8_t>(std::clamp((blurred[i] * 255) / maxValue, 0, 255));
        pixels[i * 3] = value;
        pixels[i * 3 + 1] = value;
        pixels[i * 3 + 2] = value;
    }
    return pixels;
}

int main() {

    const VideoFrames videoFrames("testhires.webm");
    constexpr int numPasses = 6;
    constexpr int spatialRadius = 4;
    FrameData previousFrame = videoFrames.getNextFrame();
    std::vector<uint8_t> previousGray = toGray(previousFrame);
    const int width = previousFrame.width;
    const int height = previousFrame.height;
    std::vector directionXPositive(static_cast<size_t>(width) * height, 0);
    std::vector directionXNegative(static_cast<size_t>(width) * height, 0);
    std::vector directionXMagnitude(static_cast<size_t>(width) * height, 0);
    std::vector directionYPositive(static_cast<size_t>(width) * height, 0);
    std::vector directionYNegative(static_cast<size_t>(width) * height, 0);
    std::vector directionYMagnitude(static_cast<size_t>(width) * height, 0);
    std::vector directionMagnitude(static_cast<size_t>(width) * height, 0);

    std::cout << "Frame size: " << width << "x" << height << std::endl;
    int completedPasses = 0;
    for (int pass = 0; pass < numPasses; ++pass) {
        FrameData nextFrame = videoFrames.getNextFrame();
        if (nextFrame.rgbPixels.empty()) {
            break;
        }
        if (nextFrame.width != previousFrame.width || nextFrame.height != previousFrame.height) {
            throw std::runtime_error("Frame size changed while calculating direction vectors");
        }
        std::vector<uint8_t> nextGray = toGray(nextFrame);

        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                const auto i = static_cast<size_t>(y) * width + x;
                const int dx = static_cast<int>(similarity(previousGray[i - 1], nextGray[i - 1]))
                    - static_cast<int>(similarity(previousGray[i + 1], nextGray[i + 1]));
                const int dy = static_cast<int>(similarity(previousGray[i - width], nextGray[i - width]))
                    - static_cast<int>(similarity(previousGray[i + width], nextGray[i + width]));
                if (dx > 0) {
                    directionXPositive[i] += dx;
                } else {
                    directionXNegative[i] -= dx;
                }
                if (dy > 0) {
                    directionYPositive[i] += dy;
                } else {
                    directionYNegative[i] -= dy;
                }
                directionXMagnitude[i] += std::abs(dx);
                directionYMagnitude[i] += std::abs(dy);
                directionMagnitude[i] += std::abs(dx) + std::abs(dy);
            }
        }

        previousFrame = std::move(nextFrame);
        previousGray = std::move(nextGray);
        ++completedPasses;
    }

    if (completedPasses == 0) {
        throw std::runtime_error("Need at least two frames to calculate direction vectors");
    }

    std::cout << "Averaged passes: " << completedPasses << std::endl;
    writePpm("directionX_positive.ppm", width, height, makeBlurredPpm(directionXPositive, width, height, spatialRadius));
    writePpm("directionX_negative.ppm", width, height, makeBlurredPpm(directionXNegative, width, height, spatialRadius));
    writePpm("directionX_magnitude.ppm", width, height, makeBlurredPpm(directionXMagnitude, width, height, spatialRadius));
    writePpm("directionY_positive.ppm", width, height, makeBlurredPpm(directionYPositive, width, height, spatialRadius));
    writePpm("directionY_negative.ppm", width, height, makeBlurredPpm(directionYNegative, width, height, spatialRadius));
    writePpm("directionY_magnitude.ppm", width, height, makeBlurredPpm(directionYMagnitude, width, height, spatialRadius));
    writePpm("direction_magnitude.ppm", width, height, makeBlurredPpm(directionMagnitude, width, height, spatialRadius));

    return 0;
}
