#include "benchmark.hpp"

#include <format>
#include <string>

void runDirectionBenchmark(const std::string& filename) {
    constexpr int maxBlockSize = 8;
    constexpr int maxSearchRadius = 16;

    for (int blockSize = 2; blockSize < maxBlockSize; ++blockSize) {
        for (int searchRadius = 2; searchRadius < maxSearchRadius; ++searchRadius) {
            runDirection(
                filename,
                blockSize,
                searchRadius,
                std::format("directiony_bs{0}_sr{1}.ppm", blockSize, searchRadius)
            );
        }
    }
}
