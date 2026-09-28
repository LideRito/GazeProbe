#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "image_io.h"
#include "oklab.h"
#include "saliency.h"

static const int LONG_EDGE = 1024;
static const double HEAT_OPACITY = 0.55;
static const double MARK[3] = {0.0, 1.0, 0.53};

class Stopwatch {
public:
    double LapMs() {
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last_).count();
        last_ = now;
        return ms;
    }

private:
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

// Five stops of the inferno colour map, linearly interpolated.
static void HeatColour(double t, double& r, double& g, double& b) {
    static const double INFERNO[5][3] = {
        {0.0015, 0.0005, 0.0139},
        {0.3411, 0.0623, 0.4290},
        {0.7355, 0.2155, 0.3303},
        {0.9779, 0.5570, 0.0350},
        {0.9884, 0.9984, 0.6449},
    };
    const double x = std::clamp(t, 0.0, 1.0) * 4.0;
    const int i = std::min(int(x), 3);
    const double f = x - i;
    r = INFERNO[i][0] + (INFERNO[i + 1][0] - INFERNO[i][0]) * f;
    g = INFERNO[i][1] + (INFERNO[i + 1][1] - INFERNO[i][1]) * f;
    b = INFERNO[i][2] + (INFERNO[i + 1][2] - INFERNO[i][2]) * f;
}

static RgbImage HeatImage(const Plane& total) {
    RgbImage out{Plane(total.rows, total.cols), Plane(total.rows, total.cols),
                 Plane(total.rows, total.cols)};
    for (std::size_t i = 0; i < total.Size(); ++i)
        HeatColour(total.v[i], out.r.v[i], out.g.v[i], out.b.v[i]);
    return out;
}

static RgbImage GreyImage(const Plane& p) {
    return RgbImage{p, p, p};
}

static void Paint(RgbImage& img, int r, int c, const double* rgb) {
    if (r < 0 || r >= img.Rows() || c < 0 || c >= img.Cols()) return;
    img.r.At(r, c) = rgb[0];
    img.g.At(r, c) = rgb[1];
    img.b.At(r, c) = rgb[2];
}

// The picture under the heat map, the edge of the top tenth, and a cross on the fixation.
static RgbImage OverlayImage(const RgbImage& picture, const Plane& total, const FixationPoint& fix) {
    RgbImage out = HeatImage(total);
    for (std::size_t i = 0; i < total.Size(); ++i) {
        out.r.v[i] = (1.0 - HEAT_OPACITY) * picture.r.v[i] + HEAT_OPACITY * out.r.v[i];
        out.g.v[i] = (1.0 - HEAT_OPACITY) * picture.g.v[i] + HEAT_OPACITY * out.g.v[i];
        out.b.v[i] = (1.0 - HEAT_OPACITY) * picture.b.v[i] + HEAT_OPACITY * out.b.v[i];
    }

    const std::vector<char> top = TopRegion(total);
    const int rows = total.rows, cols = total.cols;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const std::size_t i = std::size_t(r) * cols + c;
            if (!top[i]) continue;
            const bool edge = r == 0 || c == 0 || r == rows - 1 || c == cols - 1 ||
                              !top[i - cols] || !top[i + cols] || !top[i - 1] || !top[i + 1];
            if (edge) Paint(out, r, c, MARK);
        }
    }

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
    std::string error;
    if (!codec.SavePng(path.wstring(), img, error)) {
        std::printf("could not write %ls: %s\n", suffix, error.c_str());
        return false;
    }
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: gazeprobe <image> [output prefix]\n");
        return 2;
    }
    const std::filesystem::path input = argv[1];
    const std::filesystem::path prefix = argc > 2 ? std::filesystem::path(argv[2]) : input.stem();

    ImageCodec codec;
    if (!codec.Ok()) {
        std::printf("WIC is not available on this system\n");
        return 1;
    }

    Stopwatch clock;
    RgbImage picture;
    std::string error;
    if (!codec.Load(input.wstring(), LONG_EDGE, picture, error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    const double loadMs = clock.LapMs();

    const PreparedImage img = ToOklab(picture.r, picture.g, picture.b);
    const double oklabMs = clock.LapMs();

    SaliencyModel model;
    model.SetImage(img);
    const double modelMs = clock.LapMs();

    const FixationPoint& fix = model.Fixation();
    std::printf("image      %d x %d\n", img.Cols(), img.Rows());
    std::printf("fixation   x %.4f  y %.4f   (pixel %d, %d)\n", fix.x, fix.y,
                int(fix.x * img.Cols()), int(fix.y * img.Rows()));
    std::printf("\n%-10s %8s %10s %12s\n", "channel", "weight", "overall", "in top 10%");
    for (int c = 0; c < ChannelCount; ++c) {
        const Channel ch = Channel(c);
        std::printf("%-10s %8.2f %9.1f%% %11.1f%%\n", ChannelName(ch), model.Weight(ch),
                    100.0 * model.Share(ch), 100.0 * model.TopShare(ch));
    }
    std::printf("\nload %.1f ms   oklab %.1f ms   saliency %.1f ms\n", loadMs, oklabMs, modelMs);

    bool written = Save(codec, prefix, L"_heat.png", HeatImage(model.Total())) &&
                   Save(codec, prefix, L"_overlay.png",
                        OverlayImage(picture, model.Total(), fix));
    for (int c = 0; c < ChannelCount && written; ++c) {
        const Channel ch = Channel(c);
        const std::string name = std::string("_") + ChannelName(ch) + ".png";
        written = Save(codec, prefix, std::wstring(name.begin(), name.end()).c_str(),
                       GreyImage(model.ChannelMap(ch)));
    }
    return written ? 0 : 1;
}
