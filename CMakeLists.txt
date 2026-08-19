cmake_minimum_required(VERSION 3.20)
project(defeatghostfont)

set(CMAKE_CXX_STANDARD 23) # yipee print n format

find_package(PkgConfig REQUIRED)

pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(TESSERACT REQUIRED IMPORTED_TARGET tesseract)
pkg_check_modules(LEPTONICA REQUIRED IMPORTED_TARGET lept)

add_executable(defeatghostfont
        src/main.cpp
        src/benchmark.cpp
        src/ocr.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(defeatghostfont PRIVATE -march=native) # avx
endif()

# open mp bludz

# use threads on the auto instead of on the processing function itself
# basically instead of threading processing it just runs as many instances of processing per avail. thread
# from my testing it's generally the same or slower
# but MAY be faster
#add_compile_definitions(AUTO_THREAD_BATCH)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")

target_link_libraries(defeatghostfont PRIVATE
        PkgConfig::AVFORMAT
        PkgConfig::AVCODEC
        PkgConfig::AVUTIL
        PkgConfig::SWSCALE
        PkgConfig::TESSERACT
        PkgConfig::LEPTONICA
)
