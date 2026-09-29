#include "faces.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <filesystem>

#include "filters.h"
#include "image_io.h"
#include "pixel_utils.h"

static const int NETWORK_EDGE = 640;
static const double PADDING = 114.0;
static const float MIN_CONFIDENCE = 0.35f;
static const float MAX_OVERLAP = 0.45f;

struct Candidate {
    float x, y, w, h, score;
};

// Intersection over union: the area two boxes share, as a fraction of the area they cover together.
static float Iou(const Candidate& a, const Candidate& b) {
    const double x1 = std::max<double>(a.x, b.x), y1 = std::max<double>(a.y, b.y);
    const double x2 = std::min<double>(a.x + a.w, b.x + b.w), y2 = std::min<double>(a.y + a.h, b.y + b.h);
    if (x2 <= x1 || y2 <= y1) return 0.0f;
    const double inter = (x2 - x1) * (y2 - y1);
    return float(inter / (double(a.w) * a.h + double(b.w) * b.h - inter));
}

// The network wants a square: the picture in its top-left corner and grey everywhere else.
RgbImage Letterbox(const RgbImage& img, int edge, double& scale) {
    scale = double(edge) / std::max(img.Rows(), img.Cols());
    const int nh = ScaledSize(img.Rows(), scale);
    const int nw = ScaledSize(img.Cols(), scale);
    const double grey = PADDING / 255.0;
    RgbImage canvas{Plane(edge, edge, grey), Plane(edge, edge, grey), Plane(edge, edge, grey)};
    const Plane* from[3] = {&img.r, &img.g, &img.b};
    Plane* to[3] = {&canvas.r, &canvas.g, &canvas.b};
    for (int c = 0; c < 3; ++c) {
        Plane levels = *from[c];
        for (double& x : levels.v) x = ToLevel(x);
        // Centre-aligned like OpenCV's resize, which is how YOLOv8 letterboxes its input.
        const Plane shrunk = ResizeLinear(levels, nh, nw, Align::Centres);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) to[c]->At(y, x) = RoundHalfEven(shrunk.At(y, x)) / 255.0;
    }
    return canvas;
}

// One column per candidate: centre x, centre y, width, height and confidence, in letterboxed pixels.
std::vector<FaceBox> DecodeDetections(const Plane& output, double scale) {
    const float s = float(scale);
    std::vector<Candidate> found;
    for (int i = 0; i < output.cols; ++i) {
        const float score = float(output.At(4, i));
        if (score <= MIN_CONFIDENCE) continue;
        const float x = float(output.At(0, i)) / s, y = float(output.At(1, i)) / s;
        const float w = float(output.At(2, i)) / s, h = float(output.At(3, i)) / s;
        found.push_back({x - w / 2, y - h / 2, w, h, score});
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    // Non-maximum suppression: the network fires on many overlapping anchors per face, so keep the strongest.
    std::vector<Candidate> kept;
    for (const Candidate& c : found) {
        bool overlaps = false;
        for (const Candidate& k : kept) overlaps = overlaps || Iou(c, k) > MAX_OVERLAP;
        if (!overlaps) kept.push_back(c);
    }

    std::vector<FaceBox> boxes;
    for (const Candidate& k : kept) boxes.push_back({int(k.x), int(k.y), int(k.w), int(k.h)});
    return boxes;
}

struct FaceDetector::Session {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "gazeprobe"};
    Ort::Session net{nullptr};
    std::string inputName;
    std::string outputName;
};

FaceDetector::FaceDetector(const std::wstring& modelPath) {
    if (!std::filesystem::exists(modelPath)) {
        status_ = FaceStatus::ModelMissing;
        detail_ = std::filesystem::path(modelPath).u8string();
        return;
    }
    try {
        session_ = std::make_unique<Session>();
        session_->net = Ort::Session(session_->env, modelPath.c_str(), Ort::SessionOptions());
        Ort::AllocatorWithDefaultOptions allocator;
        session_->inputName = session_->net.GetInputNameAllocated(0, allocator).get();
        session_->outputName = session_->net.GetOutputNameAllocated(0, allocator).get();
    } catch (const std::exception& e) {
        session_.reset();
        status_ = FaceStatus::LoadFailed;
        detail_ = e.what();
    }
}

FaceDetector::~FaceDetector() = default;

bool FaceDetector::Infer(const RgbImage& input, Plane& output) {
    if (!session_) return false;
    if (input.Rows() != NETWORK_EDGE || input.Cols() != NETWORK_EDGE) {
        status_ = FaceStatus::DetectFailed;
        detail_ = "input is not a 640x640 letterbox";
        return false;
    }
    status_ = FaceStatus::Ready;
    detail_.clear();

    // A tensor is one flat block, channel-major (NCHW): all of red row by row, then all of green, then blue.
    std::vector<float> flat;
    flat.reserve(3 * input.r.Size());
    for (const Plane* p : {&input.r, &input.g, &input.b})
        for (double x : p->v) flat.push_back(float(x));

    try {
        const int64_t shape[4] = {1, 3, NETWORK_EDGE, NETWORK_EDGE};
        const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, flat.data(), flat.size(), shape, 4);
        const char* inputs[1] = {session_->inputName.c_str()};
        const char* outputs[1] = {session_->outputName.c_str()};
        std::vector<Ort::Value> result =
            session_->net.Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);

        const std::vector<int64_t> dims = result[0].GetTensorTypeAndShapeInfo().GetShape();
        if (dims.size() != 3 || dims[1] != 5) {
            status_ = FaceStatus::DetectFailed;
            detail_ = "unexpected output shape";
            return false;
        }
        // The (1, 5, count) tensor is stored row by row, the same order as a 5 x count Plane.
        const float* data = result[0].GetTensorData<float>();
        output = Plane(5, int(dims[2]));
        for (std::size_t i = 0; i < output.Size(); ++i) output.v[i] = data[i];
        return true;
    } catch (const std::exception& e) {
        status_ = FaceStatus::DetectFailed;
        detail_ = e.what();
        return false;
    }
}

std::vector<FaceBox> FaceDetector::Detect(const RgbImage& img) {
    double scale = 1.0;
    const RgbImage input = Letterbox(img, NETWORK_EDGE, scale);
    Plane output;
    if (!Infer(input, output)) return {};
    return DecodeDetections(output, scale);
}
