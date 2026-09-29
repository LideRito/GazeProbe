#include <cstdio>
#include <filesystem>

#include "check.h"
#include "image_io.h"

static RgbImage Gradient(int rows, int cols) {
    RgbImage img{Plane(rows, cols), Plane(rows, cols), Plane(rows, cols)};
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            img.r.At(r, c) = c / double(cols - 1);
            img.g.At(r, c) = r / double(rows - 1);
            img.b.At(r, c) = 0.25;
        }
    }
    return img;
}

int main() {
    Checker ck;
    std::printf("test_image_io\n");

    ImageCodec codec;
    ck.Require("WIC is available", codec.Status() == ImageStatus::Ready);
    if (codec.Status() != ImageStatus::Ready) return ck.Report("test_image_io");

    const RgbImage picture = Gradient(30, 40);
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    // A path outside ASCII, since that is exactly where C-locale file APIs give up.
    const std::filesystem::path paths[2] = {dir / L"gazeprobe_round_trip.png", dir / L"gazeprobe_画像.png"};

    for (const std::filesystem::path& path : paths) {
        const bool saved = codec.SavePng(path.wstring(), picture);
        const RgbImage back = saved ? codec.Load(path.wstring(), 1024) : RgbImage();
        const std::string tag = path == paths[0] ? "ascii path" : "non-ascii path";
        ck.Require(tag + ": saved and loaded", !back.Empty());
        if (back.Empty()) {
            std::printf("        status %d, %s\n", int(codec.Status()), codec.Detail().c_str());
            continue;
        }
        ck.Require(tag + ": same size", back.Rows() == 30 && back.Cols() == 40);
        const double step = 0.5 / 255.0 + 1e-12;
        ck.Close(tag + ": red survives", back.r, picture.r, step);
        ck.Close(tag + ": green survives", back.g, picture.g, step);
        ck.Close(tag + ": blue survives", back.b, picture.b, step);
    }

    const RgbImage shrunk = codec.Load(paths[0].wstring(), 20);
    ck.Require("shrinking keeps the aspect", shrunk.Rows() == 15 && shrunk.Cols() == 20);

    const RgbImage untouched = codec.Load(paths[0].wstring(), 4096);
    ck.Require("never enlarges", untouched.Cols() == 40);

    const RgbImage missing = codec.Load((dir / L"gazeprobe_missing.png").wstring(), 1024);
    ck.Require("a missing file is an error, not a crash",
               missing.Empty() && codec.Status() == ImageStatus::OpenFailed);
    ck.Require("and the next success clears it",
               !codec.Load(paths[0].wstring(), 1024).Empty() && codec.Status() == ImageStatus::Ready);

    for (const std::filesystem::path& path : paths) std::filesystem::remove(path);
    return ck.Report("test_image_io");
}
