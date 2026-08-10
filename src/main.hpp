#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct AVFormatContext;
struct SwsContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;

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

struct DirectionResult {
    int width = 0;
    int height = 0;
    std::vector<int16_t> directionY = {};
};

class VideoFrames {
private:
    int videoStreamIndex = -1;
    std::string filename;
    AVFormatContext* fmtCtx = nullptr;
    SwsContext* swsCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgbFrame = nullptr;

public:
    explicit VideoFrames(std::string filename);
    ~VideoFrames();

    VideoFrames(const VideoFrames&) = delete;
    VideoFrames& operator=(const VideoFrames&) = delete;

    FrameData getNextFrame() const;
};

// fastest one, good for benchmarks where we only need 2 frames
DirectionResult runDirection(const FrameData& firstFrame, const FrameData& secondFrame,  int blockSize,  int searchRadius, const std::string& outputFilename);
// helper one, just makes it nicer writing the filename one ig
DirectionResult runDirection(const VideoFrames& videoFrames, int blockSize,  int searchRadius, const std::string& outputFilename);
// easiest one for commands, does everything itself
DirectionResult runDirection(const std::string& filename,  int blockSize,  int searchRadius, const std::string & outputFilename);
void runDirectionBenchmark(const std::string& filename);
double calculateDirectionYVarianceRatio(const std::vector<int16_t>& directionYmap, int width, int height);
std::string textFromDirectionResult(const DirectionResult& result, int ocrDenoiseBrickSize);
