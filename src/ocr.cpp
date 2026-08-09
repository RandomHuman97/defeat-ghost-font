#include "main.hpp"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {
constexpr int ocrMaxImageWidth = 640;
constexpr int ocrDenoiseBrickSize = 5;

struct PixDeleter {
    void operator()(Pix* pix) const {
        pixDestroy(&pix);
    }
};

struct TessTextDeleter {
    void operator()(char* text) const {
        delete[] text;
    }
};

std::unique_ptr<Pix, PixDeleter> directionResultToPix(const DirectionResult& result) {
    if (result.width <= 0 || result.height <= 0) {
        throw std::runtime_error("Invalid OCR image dimensions");
    }
    if (result.directionY.size() != static_cast<size_t>(result.width * result.height)) {
        throw std::runtime_error("Direction result does not match OCR image dimensions");
    }

    std::unique_ptr<Pix, PixDeleter> pix(pixCreate(result.width, result.height, 1));
    if (!pix) {
        throw std::runtime_error("Could not allocate OCR image");
    }

    l_uint32* data = pixGetData(pix.get());
    const int wpl = pixGetWpl(pix.get());
    for (int y = 0; y < result.height; ++y) {
        l_uint32* line = data + y * wpl;
        for (int x = 0; x < result.width; ++x) {
            if (result.directionY[static_cast<size_t>(y * result.width + x)] <= 0) {
                SET_DATA_BIT(line, x);
            }
        }
    }

    return pix;
}

std::unique_ptr<Pix, PixDeleter> scalePixForOcr(std::unique_ptr<Pix, PixDeleter> pix) {
    const int width = pixGetWidth(pix.get());
    const int height = pixGetHeight(pix.get());
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid OCR image dimensions");
    }
    if (width == ocrMaxImageWidth) {
        return pix;
    }

    const int scaledWidth = ocrMaxImageWidth;
    const int scaledHeight = std::max(1, height * scaledWidth / width);
    Pix* scaled = pixScaleToSize(pix.get(), scaledWidth, scaledHeight);
    if (!scaled) {
        throw std::runtime_error("Could not scale OCR image");
    }

    Pix* binary = pixConvertTo1(scaled, 128);
    pixDestroy(&scaled);
    if (!binary) {
        throw std::runtime_error("Could not binarize scaled OCR image");
    }

    return std::unique_ptr<Pix, PixDeleter>(binary);
}

std::unique_ptr<Pix, PixDeleter> denoisePixForOcr(std::unique_ptr<Pix, PixDeleter> pix) {
    Pix* closed = pixCloseBrick(nullptr, pix.get(), ocrDenoiseBrickSize, ocrDenoiseBrickSize);
    if (!closed) {
        throw std::runtime_error("Could not close OCR image noise");
    }

    Pix* opened = pixOpenBrick(nullptr, closed, ocrDenoiseBrickSize, ocrDenoiseBrickSize);
    pixDestroy(&closed);
    if (!opened) {
        throw std::runtime_error("Could not open OCR image noise");
    } //

    return std::unique_ptr<Pix, PixDeleter>(opened);
}

std::unique_ptr<Pix, PixDeleter> preparePixForOcr(const DirectionResult& result) {
    return denoisePixForOcr(
        scalePixForOcr(
            directionResultToPix(result)
            )
        );
}
}

std::string textFromDirectionResult(const DirectionResult& result) {
    std::unique_ptr<Pix, PixDeleter> pix = preparePixForOcr(result);
    writeDirectionResultOcrImage("ocr.png",result);
    tesseract::TessBaseAPI api;
    if (api.Init(nullptr, "eng") != 0) {
        throw std::runtime_error("Could not initialize Tesseract OCR");
    }

    api.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
    api.SetImage(pix.get());

    std::unique_ptr<char[], TessTextDeleter> outText(api.GetUTF8Text());
    api.End();
    if (!outText) {
        throw std::runtime_error("Tesseract OCR did not return text");
    }

    return {outText.get()};
}

void writeDirectionResultOcrImage(const std::string& filename, const DirectionResult& result) {
    std::unique_ptr<Pix, PixDeleter> pix = preparePixForOcr(result);
    if (pixWrite(filename.c_str(), pix.get(), IFF_PNG) != 0) {
        throw std::runtime_error("Could not write OCR debug image: " + filename);
    }
}
