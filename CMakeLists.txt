cmake_minimum_required(VERSION 3.20)
project(defeatghostfont)

set(CMAKE_CXX_STANDARD 17)

# 1. Find pkg-config tool
find_package(PkgConfig REQUIRED)

# 2. Locate FFmpeg libraries via pkg-config
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)

add_executable(defeatghostfont oldmain.cpp
        main.cpp)

# 3. Link the actual FFmpeg libraries to your executable
target_link_libraries(defeatghostfont PRIVATE
        PkgConfig::AVFORMAT
        PkgConfig::AVCODEC
        PkgConfig::AVUTIL
        PkgConfig::SWSCALE
)