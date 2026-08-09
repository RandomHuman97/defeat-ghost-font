#include "../main.hpp"

#include <format>
#include <string>

void runDirectionBenchmark(const std::string& filename) {
    constexpr int maxBlockSize = 8;
    constexpr int maxSearchRadius = 16;
    // create video object for it
    VideoFrames videoFrames(filename);
    videoFrames.getNextFrame(); // get first one since for some reason ghost font has blank first frame
    FrameData firstFrame = videoFrames.getNextFrame();
    FrameData secondFrame = videoFrames.getNextFrame();
    for (int blockSize = 2; blockSize <= maxBlockSize; ++blockSize) {
        for (int searchRadius = 2; searchRadius <= maxSearchRadius; ++searchRadius) {
            runDirection(
                firstFrame,
                secondFrame,
                blockSize,
                searchRadius,
                std::format("directiony_bs{0}_sr{1}.ppm", blockSize, searchRadius)
            );
        }
    }
}
