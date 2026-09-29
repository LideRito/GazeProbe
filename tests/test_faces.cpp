#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "check.h"
#include "faces.h"
#include "image_io.h"

// One network output with room for `count` detections, all below the confidence threshold.
static std::vector<float> EmptyOutput(int count) {
    return std::vector<float>(5 * std::size_t(count), 0.0f);
}

static void Put(std::vector<float>& out, int count, int i, float cx, float cy, float w, float h, float score) {
    out[i] = cx;
    out[count + i] = cy;
    out[2 * count + i] = w;
    out[3 * count + i] = h;
    out[4 * count + i] = score;
}

int main() {
    Checker ck;
    std::printf("test_faces\n");

    // A wide picture: it fills the top of the square, and the padding below stays grey.
    RgbImage wide{Plane(20, 40, 1.0), Plane(20, 40, 0.0), Plane(20, 40, 0.5)};
    double scale = 0.0;
    const std::vector<float> blob = Letterbox(wide, 16, scale);
    const std::size_t area = 16 * 16;
    ck.Close("scale fits the long edge", scale, 16.0 / 40.0, 0.0);
    ck.Require("planar, three channels", blob.size() == 3 * area);
    ck.Close("red comes first", blob[0], 1.0, 0.0);
    ck.Close("then green", blob[area], 0.0, 0.0);
    ck.Close("then blue, rounded to a byte", blob[2 * area], 128.0 / 255.0, 1e-7);
    ck.Close("the picture ends at row 8", blob[7 * 16 + 15], 1.0, 0.0);
    ck.Close("and grey padding starts there", blob[8 * 16], 114.0 / 255.0, 1e-7);

    const int count = 4;
    std::vector<float> none = EmptyOutput(count);
    ck.Require("nothing above the threshold, no faces", DecodeDetections(none.data(), count, 1.0).empty());

    std::vector<float> out = EmptyOutput(count);
    Put(out, count, 0, 100.0f, 100.0f, 40.0f, 60.0f, 0.9f);
    Put(out, count, 1, 104.0f, 102.0f, 40.0f, 60.0f, 0.8f);
    Put(out, count, 2, 300.0f, 100.0f, 40.0f, 40.0f, 0.6f);
    Put(out, count, 3, 500.0f, 500.0f, 40.0f, 40.0f, 0.2f);
    const std::vector<FaceBox> faces = DecodeDetections(out.data(), count, 0.5);
    ck.Require("an overlapping duplicate and a weak box are dropped", faces.size() == 2);
    if (faces.size() == 2) {
        ck.Require("the strongest comes first", faces[0].x == 160 && faces[0].y == 140);
        ck.Require("sizes are scaled back to the picture", faces[0].w == 80 && faces[0].h == 120);
        ck.Require("a separate face survives", faces[1].x == 560 && faces[1].w == 80);
    }

    std::vector<float> edge = EmptyOutput(1);
    Put(edge, 1, 0, 5.0f, 5.0f, 20.0f, 20.0f, 0.9f);
    const std::vector<FaceBox> cut = DecodeDetections(edge.data(), 1, 1.0);
    ck.Require("a box over the top-left edge keeps its negative corner", cut.size() == 1 && cut[0].x == -5);

    const std::filesystem::path missing = std::filesystem::temp_directory_path() / L"gazeprobe_no_model.onnx";
    FaceDetector detector(missing.wstring());
    ck.Require("a missing model is reported", detector.Status() == FaceStatus::ModelMissing);
    ck.Require("and finds nothing rather than failing", detector.Detect(wide).empty());

    return ck.Report("test_faces");
}
