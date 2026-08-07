#include <cstdint>
#include <string>
#include <utility>
#include <vector>
struct FrameData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb_pixels = {}; // Packed RGB24 buffer
};
class VideoFrames {
private:
std::string filename;
public:
 explicit VideoFrames(std::string filename)  :
    filename(std::move(filename)) {

 }
    FrameData getNextFrame() {
        return {}; //stub for now
 }
};