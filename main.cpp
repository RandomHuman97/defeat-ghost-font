#include <fstream>
#include <iostream>
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

struct PaddedFrameData {
    int width = 0;
    int height = 0;
    int stride = 0;
    int yPad = 0;
    std::vector<uint8_t> rgbPixels = {};
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

static inline int pixelDiff(const uint8_t a, const uint8_t b) {
    return a > b ? a - b : b - a;
}

static inline int ceilDiv(const int value, const int divisor) {
    return (value + divisor - 1) / divisor;
}

static inline int clampInt(const int value, const int low, const int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static PaddedFrameData padFrame(const FrameData& frame, const int blockSize, const int yPad) {
    constexpr int channels = 3;
    PaddedFrameData padded;
    padded.width = ceilDiv(frame.width, blockSize) * blockSize;
    padded.height = ceilDiv(frame.height, blockSize) * blockSize;
    padded.stride = padded.width * channels;
    padded.yPad = yPad;
    padded.rgbPixels.resize(static_cast<size_t>((padded.height + yPad * 2) * padded.stride));

    const int sourceStride = frame.width * channels;
    for (int y = -yPad; y < padded.height + yPad; ++y) {
        const int sourceY = clampInt(y, 0, frame.height - 1);
        uint8_t* out = padded.rgbPixels.data() + static_cast<size_t>((y + yPad) * padded.stride);
        const uint8_t* sourceRow = frame.rgbPixels.data() + static_cast<size_t>(sourceY * sourceStride);

        for (int x = 0; x < padded.width; ++x) {
            const int sourceX = x < frame.width ? x : frame.width - 1;
            const uint8_t* sourcePixel = sourceRow + sourceX * channels;
            out[x * channels] = sourcePixel[0];
            out[x * channels + 1] = sourcePixel[1];
            out[x * channels + 2] = sourcePixel[2];
        }
    }

    return padded;
}

static inline int stampDiffBlock(const uint8_t* first, const uint8_t* second, const int stride, const int dy, const int blockSize) {
    const uint8_t* secondShifted = second + dy * stride;
    const int rowBytes = blockSize * 3;
    int diff = 0;

    for (int y = 0; y < blockSize; ++y) {
        const uint8_t* firstRow = first + y * stride;
        const uint8_t* secondRow = secondShifted + y * stride;
        for (int i = 0; i < rowBytes; ++i) {
            diff += pixelDiff(firstRow[i], secondRow[i]);
        }
    }

    return diff;
}

static inline int16_t bestVerticalShiftBlock(const uint8_t* first, const uint8_t* second, const int stride, const int blockSize, const int searchRadius) {
    int16_t bestDy = static_cast<int16_t>(-searchRadius);
    int bestDiff = stampDiffBlock(first, second, stride, -searchRadius, blockSize);

    for (int16_t dy = static_cast<int16_t>(-searchRadius + 1); dy <= searchRadius; ++dy) {
        const int diff = stampDiffBlock(first, second, stride, dy, blockSize);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestDy = dy;
        }
    }

    return bestDy;
}

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

static Rgb directionToColor(const int16_t value, const int valueRange) {
    auto magnitude = static_cast<uint8_t>(static_cast<float>(value)/static_cast<float>(valueRange) * 255);
    if (value>0) {
        return {magnitude, 0,0};
    }
    return {0,0,0};
    /*if (value < 0) {
        return {0,magnitude,0};
    }
    return {50,50,50};
    */
}

static void writeDirectionPpm(const std::string& filename, const std::vector<int16_t>& direction, const int width, const int height, const int searchRadius) {
    if (direction.size() != static_cast<size_t>(width * height)) {
        throw std::runtime_error("Direction buffer does not match output dimensions");
    }

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Could not open output file: " + filename);
    }

    out << "P6\n" << width << ' ' << height << "\n255\n";
    std::vector<uint8_t> rgbDirection(direction.size() * 3);
    size_t rgbIndex = 0;
    for (const int16_t value : direction) {
        const Rgb color = directionToColor(value, searchRadius );
        rgbDirection[rgbIndex++] = color.r;
        rgbDirection[rgbIndex++] = color.g;
        rgbDirection[rgbIndex++] = color.b;
    }
    out.write(reinterpret_cast<const char*>(rgbDirection.data()), static_cast<std::streamsize>(rgbDirection.size()));
}

int main() {

    const VideoFrames videoFrames("test.webm");
    // fetch first frame because for some reason ghost font output vids have a freeze at first frame ig
    videoFrames.getNextFrame();
    const FrameData firstFrame = videoFrames.getNextFrame();
    const FrameData secondFrame = videoFrames.getNextFrame();
    if (firstFrame.width != secondFrame.width || firstFrame.height != secondFrame.height) {
        throw std::runtime_error("Frame sizes do not match");
    }

    constexpr int blockSize = 4;
    constexpr int searchRadius = 12;
    if constexpr (blockSize <= 0 || searchRadius < 0) {
        throw std::runtime_error("Invalid block/search size");
    }

    const PaddedFrameData paddedFirstFrame = padFrame(firstFrame, blockSize, searchRadius);
    const PaddedFrameData paddedSecondFrame = padFrame(secondFrame, blockSize, searchRadius);
    const int directionWidth = paddedFirstFrame.width / blockSize;
    const int directionHeight = paddedFirstFrame.height / blockSize;
    std::vector<int16_t> directionY(static_cast<size_t>(directionWidth * directionHeight), 0);

    std::cout << "Size of first frame: " << firstFrame.width << "x" << firstFrame.height << std::endl;
    std::cout << "Size of second frame: " << secondFrame.width << "x" << secondFrame.height << std::endl;
    constexpr int channels = 3;
    const int stride = paddedFirstFrame.stride;
    for (int blockY = 0; blockY < directionHeight; ++blockY) {
        const int y = paddedFirstFrame.yPad + blockY * blockSize;
        for (int blockX = 0; blockX < directionWidth; ++blockX) {
            const int x = blockX * blockSize;
            const size_t rgbIndex = static_cast<size_t>(y * stride + x * channels);
            directionY[static_cast<size_t>(blockY * directionWidth + blockX)] = bestVerticalShiftBlock(
                paddedFirstFrame.rgbPixels.data() + rgbIndex,
                paddedSecondFrame.rgbPixels.data() + rgbIndex,
                stride,
                blockSize,
                searchRadius
            );
        }
    }

    writeDirectionPpm("direction_y.ppm", directionY, directionWidth, directionHeight, searchRadius);
    return 0;
}
