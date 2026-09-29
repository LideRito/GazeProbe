#pragma once

#include <memory>
#include <string>
#include <vector>

struct Plane;
struct RgbImage;

// A face in pixels of the analysed image: top-left corner and size.
struct FaceBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

enum class FaceStatus { Ready, ModelMissing, LoadFailed, DetectFailed };

double LetterboxScale(const RgbImage& img, int edge);
RgbImage Letterbox(const RgbImage& img, int edge);
std::vector<FaceBox> DecodeDetections(const Plane& output, double scale);

// Runs a YOLOv8 face detector through ONNX Runtime; without a model it reports why and finds nothing.
class FaceDetector {
public:
    explicit FaceDetector(const std::wstring& modelPath);
    ~FaceDetector();
    FaceDetector(const FaceDetector&) = delete;
    FaceDetector& operator=(const FaceDetector&) = delete;

    FaceStatus Status() const { return status_; }
    const std::string& Detail() const { return detail_; }

    // The raw network output, 5 rows by one column per candidate; empty on failure.
    Plane Infer(const RgbImage& input);
    std::vector<FaceBox> Detect(const RgbImage& img);

private:
    struct Session;
    std::unique_ptr<Session> session_;
    FaceStatus status_ = FaceStatus::Ready;
    std::string detail_;
};
