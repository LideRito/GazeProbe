#include <cstdio>
#include <filesystem>
#include <vector>

#include "check.h"
#include "faces.h"
#include "image_io.h"

// Fills candidate i of a network output: centre, size and confidence.
static void Put(Plane& out, int i, double cx, double cy, double w, double h, double score) {
    out.At(0, i) = cx;
    out.At(1, i) = cy;
    out.At(2, i) = w;
    out.At(3, i) = h;
    out.At(4, i) = score;
}

int main() {
    Checker ck;
    std::printf("test_faces\n");

    // A wide picture: it fills the top of the square, and the padding below stays grey.
    RgbImage wide{Plane(20, 40, 1.0), Plane(20, 40, 0.0), Plane(20, 40, 0.5)};
    double scale = 0.0;
    const RgbImage canvas = Letterbox(wide, 16, scale);
    ck.Close("scale fits the long edge", scale, 16.0 / 40.0, 0.0);
    ck.Require("the canvas is square", canvas.Rows() == 16 && canvas.Cols() == 16);
    ck.Close("red carries over", canvas.r.At(0, 0), 1.0, 0.0);
    ck.Close("green carries over", canvas.g.At(0, 0), 0.0, 0.0);
    ck.Close("blue is rounded to a byte", canvas.b.At(0, 0), 128.0 / 255.0, 1e-15);
    ck.Close("the picture ends at row 8", canvas.r.At(7, 15), 1.0, 0.0);
    ck.Close("and grey padding starts there", canvas.r.At(8, 0), 114.0 / 255.0, 1e-15);

    ck.Require("nothing above the threshold, no faces", DecodeDetections(Plane(5, 4), 1.0).empty());

    Plane out(5, 4);
    Put(out, 0, 100.0, 100.0, 40.0, 60.0, 0.9);
    Put(out, 1, 104.0, 102.0, 40.0, 60.0, 0.8);
    Put(out, 2, 300.0, 100.0, 40.0, 40.0, 0.6);
    Put(out, 3, 500.0, 500.0, 40.0, 40.0, 0.2);
    const std::vector<FaceBox> faces = DecodeDetections(out, 0.5);
    ck.Require("an overlapping duplicate and a weak box are dropped", faces.size() == 2);
    if (faces.size() == 2) {
        ck.Require("the strongest comes first", faces[0].x == 160 && faces[0].y == 140);
        ck.Require("sizes are scaled back to the picture", faces[0].w == 80 && faces[0].h == 120);
        ck.Require("a separate face survives", faces[1].x == 560 && faces[1].w == 80);
    }

    Plane edge(5, 1);
    Put(edge, 0, 5.0, 5.0, 20.0, 20.0, 0.9);
    const std::vector<FaceBox> cut = DecodeDetections(edge, 1.0);
    ck.Require("a box over the top-left edge keeps its negative corner", cut.size() == 1 && cut[0].x == -5);

    const std::filesystem::path missing = std::filesystem::temp_directory_path() / L"gazeprobe_no_model.onnx";
    FaceDetector detector(missing.wstring());
    ck.Require("a missing model is reported", detector.Status() == FaceStatus::ModelMissing);
    ck.Require("and finds nothing rather than failing", detector.Detect(wide).empty());

    return ck.Report("test_faces");
}
