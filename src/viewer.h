#pragma once

#include <array>
#include <future>
#include <string>
#include <vector>

#include "faces.h"
#include "image_io.h"
#include "renderer.h"
#include "saliency.h"

// Everything worked out for one picture, built off the UI thread.
struct Analysis {
    std::wstring path;
    RgbImage picture;
    ImageStatus imageStatus = ImageStatus::Ready;
    std::string imageDetail;
    std::vector<FaceBox> faces;
    FaceStatus faceStatus = FaceStatus::Ready;
    std::string faceDetail;
    SaliencyModel model;
    double loadMs = 0.0;
    double oklabMs = 0.0;
    double facesMs = 0.0;
    double saliencyMs = 0.0;
};

// One picture, its saliency, and the controls over it; slow work runs in the background.
class Viewer {
public:
    Viewer(Renderer& renderer, HWND__* window);

    void Open(const std::wstring& path);
    void Frame();

private:
    bool HasImage() const { return !current_.picture.Empty(); }
    void Collect();
    void SetWeight(Channel c, float w);
    void UploadResult();
    void UploadChannelMap();
    void DrawImage();
    void DrawPanel();

    Renderer& renderer_;
    HWND__* window_;
    FaceDetector detector_;
    std::future<Analysis> pending_;
    Analysis current_;

    std::array<float, ChannelCount> weights_;
    int view_ = 0;
    float opacity_;
    bool showRegion_ = true;
    bool showFixation_ = true;
    bool showFaces_ = true;
    double recombineMs_ = 0.0;
};
