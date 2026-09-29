#pragma once

#include <memory>
#include <string>
#include <vector>

struct RgbImage;

// A face in pixels of the analysed image: top-left corner and size.
struct FaceBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

enum class FaceStatus { Ready, ModelMissing, LoadFailed, DetectFailed };

std::vector<float> Letterbox(const RgbImage& img, int edge, double& scale);
std::vector<FaceBox> DecodeDetections(const float* output, int count, double scale);

// Runs a YOLOv8 face detector through ONNX Runtime; without a model it reports why and finds nothing.
class FaceDetector {
public:
    explicit FaceDetector(const std::wstring& modelPath);
    ~FaceDetector();
    FaceDetector(const FaceDetector&) = delete;
    FaceDetector& operator=(const FaceDetector&) = delete;

    FaceStatus Status() const { return status_; }
    const std::string& Detail() const { return detail_; }

    bool Infer(const std::vector<float>& input, std::vector<float>& output);
    std::vector<FaceBox> Detect(const RgbImage& img);

private:
    struct Session;
    std::unique_ptr<Session> session_;
    FaceStatus status_ = FaceStatus::Ready;
    std::string detail_;
};
