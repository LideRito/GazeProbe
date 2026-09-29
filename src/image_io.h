#pragma once

#include <string>

#include "plane.h"

struct IWICImagingFactory;

struct RgbImage {
    Plane r, g, b;

    int Rows() const { return r.rows; }
    int Cols() const { return r.cols; }
};

// Reads and writes images through WIC, the imaging API built into Windows.
class ImageCodec {
public:
    ImageCodec();
    ~ImageCodec();
    ImageCodec(const ImageCodec&) = delete;
    ImageCodec& operator=(const ImageCodec&) = delete;

    bool Ok() const { return factory_ != nullptr; }

    // Transparency is flattened onto white, and the image is shrunk to longEdge but never enlarged.
    bool Load(const std::wstring& path, int longEdge, RgbImage& out, std::string& error);
    bool SavePng(const std::wstring& path, const RgbImage& img, std::string& error);

private:
    IWICImagingFactory* factory_ = nullptr;
    bool comStarted_ = false;
};
