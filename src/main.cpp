#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app_utils.h"
#include "faces.h"
#include "image_io.h"
#include "oklab.h"
#include "overlay_utils.h"
#include "saliency.h"

static const char* Describe(ImageStatus status) {
    switch (status) {
    case ImageStatus::Ready: return "ready";
    case ImageStatus::Unavailable: return "WIC is not available on this system";
    case ImageStatus::OpenFailed: return "could not open the image";
    case ImageStatus::DecodeFailed: return "could not decode the image";
    case ImageStatus::ResizeFailed: return "could not resize the image";
    case ImageStatus::WriteFailed: return "could not write the image";
    }
    return "unknown";
}

static const char* Describe(FaceStatus status) {
    switch (status) {
    case FaceStatus::Ready: return "ready";
    case FaceStatus::ModelMissing: return "model not found";
    case FaceStatus::LoadFailed: return "model failed to load";
    case FaceStatus::DetectFailed: return "detection failed";
    }
    return "unknown";
}

static void Paint(RgbImage& img, int r, int c, const double* rgb) {
    if (r < 0 || r >= img.Rows() || c < 0 || c >= img.Cols()) return;
    img.r.At(r, c) = rgb[0];
    img.g.At(r, c) = rgb[1];
    img.b.At(r, c) = rgb[2];
}

static void Outline(RgbImage& img, const FaceBox& f) {
    for (int t = 0; t < 2; ++t) {
        for (int c = f.x; c < f.x + f.w; ++c) {
            Paint(img, f.y + t, c, FACE_MARK);
            Paint(img, f.y + f.h - 1 - t, c, FACE_MARK);
        }
        for (int r = f.y; r < f.y + f.h; ++r) {
            Paint(img, r, f.x + t, FACE_MARK);
            Paint(img, r, f.x + f.w - 1 - t, FACE_MARK);
        }
    }
}

// The picture under the heat map, the detected faces, the edge of the top tenth, and a cross on the fixation.
static RgbImage OverlayImage(const RgbImage& picture, const SaliencyModel& model,
                             const std::vector<FaceBox>& faces) {
    const Plane& total = model.Total();
    const FixationPoint& fix = model.Fixation();
    RgbImage out = HeatImage(total);
    for (std::size_t i = 0; i < total.Size(); ++i) {
        out.r.v[i] = (1.0 - HEAT_OPACITY) * picture.r.v[i] + HEAT_OPACITY * out.r.v[i];
        out.g.v[i] = (1.0 - HEAT_OPACITY) * picture.g.v[i] + HEAT_OPACITY * out.g.v[i];
        out.b.v[i] = (1.0 - HEAT_OPACITY) * picture.b.v[i] + HEAT_OPACITY * out.b.v[i];
    }

    for (const FaceBox& f : faces) Outline(out, f);

    const int rows = total.rows, cols = total.cols;
    const std::vector<char> edge = RegionEdge(model.Top(), rows, cols);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if (edge[std::size_t(r) * cols + c]) Paint(out, r, c, MARK);

    const int cr = int(fix.y * rows), cc = int(fix.x * cols);
    const int arm = std::max(rows, cols) / 40;
    for (int d = -arm; d <= arm; ++d) {
        for (int w = -1; w <= 1; ++w) {
            Paint(out, cr + w, cc + d, MARK);
            Paint(out, cr + d, cc + w, MARK);
        }
    }
    return out;
}

static bool Save(ImageCodec& codec, const std::filesystem::path& prefix, const wchar_t* suffix,
                 const RgbImage& img) {
    std::filesystem::path path = prefix;
    path += suffix;
    if (!codec.SavePng(path.wstring(), img)) {
        std::printf("%ls: %s (%s)\n", suffix, Describe(codec.Status()), codec.Detail().c_str());
        return false;
    }
    return true;
}

int wmain(int argc, wchar_t** argv) {
    // Paths and error texts are printed as UTF-8, which a Chinese or Japanese console would otherwise misread.
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::wstring> positional;
    std::wstring modelPath = DefaultModelPath();
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--model" && i + 1 < argc) modelPath = argv[++i];
        else positional.push_back(argv[i]);
    }
    if (positional.empty()) {
        std::printf("usage: gazeprobe <image> [output prefix] [--model <face model.onnx>]\n");
        return 2;
    }
    const std::filesystem::path input = positional[0];
    const std::filesystem::path prefix =
        positional.size() > 1 ? std::filesystem::path(positional[1]) : input.stem();

    ImageCodec codec;
    if (codec.Status() != ImageStatus::Ready) {
        std::printf("%s\n", Describe(codec.Status()));
        return 1;
    }

    Stopwatch clock;
    const RgbImage picture = codec.Load(input.wstring(), LONG_EDGE);
    if (picture.Empty()) {
        std::printf("%s (%s)\n", Describe(codec.Status()), codec.Detail().c_str());
        return 1;
    }
    const double loadMs = clock.LapMs();

    const PreparedImage img = ToOklab(picture.r, picture.g, picture.b);
    const double oklabMs = clock.LapMs();

    FaceDetector detector(modelPath);
    const double modelLoadMs = clock.LapMs();
    const std::vector<FaceBox> faces = detector.Detect(picture);
    const double facesMs = clock.LapMs();

    SaliencyModel model;
    model.SetImage(img, faces);
    const double saliencyMs = clock.LapMs();

    const FixationPoint& fix = model.Fixation();
    std::printf("image      %d x %d\n", img.Cols(), img.Rows());
    if (detector.Status() == FaceStatus::Ready) {
        std::printf("faces      %d\n", int(faces.size()));
        for (const FaceBox& f : faces) std::printf("           at %d, %d  size %d x %d\n", f.x, f.y, f.w, f.h);
    } else {
        std::printf("faces      off: %s (%s)\n", Describe(detector.Status()), detector.Detail().c_str());
    }
    std::printf("fixation   x %.4f  y %.4f   (pixel %d, %d)\n", fix.x, fix.y,
                int(fix.x * img.Cols()), int(fix.y * img.Rows()));
    std::printf("\n%-10s %8s %10s %12s\n", "channel", "weight", "overall", "in top 10%");
    for (int c = 0; c < ChannelCount; ++c) {
        const Channel ch = Channel(c);
        std::printf("%-10s %8.2f %9.1f%% %11.1f%%\n", ChannelName(ch), model.Weight(ch),
                    100.0 * model.Share(ch), 100.0 * model.TopShare(ch));
    }
    std::printf("\nload %.1f ms   oklab %.1f ms   face model %.1f ms   faces %.1f ms   saliency %.1f ms\n",
                loadMs, oklabMs, modelLoadMs, facesMs, saliencyMs);

    bool written = Save(codec, prefix, L"_heat.png", HeatImage(model.Total())) &&
                   Save(codec, prefix, L"_overlay.png",
                        OverlayImage(picture, model, faces));
    for (int c = 0; c < ChannelCount && written; ++c) {
        const Channel ch = Channel(c);
        const std::string name = std::string("_") + ChannelName(ch) + ".png";
        written = Save(codec, prefix, std::wstring(name.begin(), name.end()).c_str(),
                       GreyImage(model.ChannelMap(ch)));
    }
    return written ? 0 : 1;
}
