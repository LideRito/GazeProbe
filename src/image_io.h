#pragma once

#include <string>

#include "plane.h"

struct IWICImagingFactory;

struct RgbImage {
    Plane r, g, b;

    int Rows() const { return r.rows; }
    int Cols() const { return r.cols; }
    bool Empty() const { return r.Empty(); }
};

enum class ImageStatus { Ready, Unavailable, OpenFailed, DecodeFailed, ResizeFailed, WriteFailed };

// Reads and writes images through WIC, the imaging API built into Windows.
class ImageCodec {
public:
    ImageCodec();
    ~ImageCodec();
    ImageCodec(const ImageCodec&) = delete;
    ImageCodec& operator=(const ImageCodec&) = delete;

    ImageStatus Status() const { return status_; }
    const std::string& Detail() const { return detail_; }

    // Flattens transparency onto white and shrinks to longEdge, never enlarging; empty on failure.
    RgbImage Load(const std::wstring& path, int longEdge);
    bool SavePng(const std::wstring& path, const RgbImage& img);

private:
    IWICImagingFactory* factory_ = nullptr;
    bool comStarted_ = false;
    ImageStatus status_ = ImageStatus::Ready;
    std::string detail_;
};
