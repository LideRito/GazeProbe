#include "viewer.h"

#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

#include "app_utils.h"
#include "filters.h"
#include "labels_ja.h"
#include "oklab.h"
#include "overlay_utils.h"

using Microsoft::WRL::ComPtr;

static ImU32 PackColour(const double* rgb) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(float(rgb[0]), float(rgb[1]), float(rgb[2]), 1.0f));
}

static const ImU32 MARK_COLOUR = PackColour(MARK);
static const ImU32 FACE_COLOUR = PackColour(FACE_MARK);
static const ImVec4 WARNING = ImVec4(1.0f, 0.45f, 0.4f, 1.0f);

// The view list: the picture under the heat map, the heat map alone, then one entry per channel map.
static const int OVERLAY_VIEW = 0;
static const int TOTAL_VIEW = 1;
static const int FIRST_CHANNEL_VIEW = 2;

// Everything slow that happens once per picture; runs on a worker thread with its own COM apartment.
static Analysis Analyse(const std::wstring& path, FaceDetector& detector,
                        const std::array<float, ChannelCount>& weights) {
    Analysis a;
    a.path = path;
    Stopwatch clock;
    ImageCodec codec;
    a.picture = codec.Load(path, LONG_EDGE);
    a.imageStatus = codec.Status();
    a.imageDetail = codec.Detail();
    if (a.picture.Empty()) return a;
    a.loadMs = clock.LapMs();

    const PreparedImage img = ToOklab(a.picture.r, a.picture.g, a.picture.b);
    a.oklabMs = clock.LapMs();

    a.faces = detector.Detect(a.picture);
    a.faceStatus = detector.Status();
    a.faceDetail = detector.Detail();
    a.facesMs = clock.LapMs();

    for (int c = 0; c < ChannelCount; ++c) a.model.SetWeight(Channel(c), weights[c]);
    a.model.SetImage(img, a.faces);
    a.saliencyMs = clock.LapMs();
    return a;
}

// The standard Windows open dialog, limited to pictures; empty when cancelled.
static std::wstring AskForImage(HWND__* owner) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return {};
    const COMDLG_FILTERSPEC types[1] = {{L"画像", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp"}};
    dialog->SetFileTypes(1, types);
    ComPtr<IShellItem> item;
    if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(&item))) return {};
    PWSTR name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) return {};
    const std::wstring path = name;
    CoTaskMemFree(name);
    return path;
}

Viewer::Viewer(Renderer& renderer, HWND__* window)
    : renderer_(renderer), window_(window), detector_(DefaultModelPath()), opacity_(float(HEAT_OPACITY)) {
    const SaliencyModel defaults;
    for (int c = 0; c < ChannelCount; ++c) weights_[c] = float(defaults.Weight(Channel(c)));
}

void Viewer::Open(const std::wstring& path) {
    if (pending_.valid()) return;
    const std::array<float, ChannelCount> weights = weights_;
    pending_ = std::async(std::launch::async,
                          [this, path, weights] { return Analyse(path, detector_, weights); });
}

void Viewer::Collect() {
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    current_ = pending_.get();
    if (!HasImage()) return;

    // The sliders may have moved while the picture was being worked out.
    for (int c = 0; c < ChannelCount; ++c) {
        if (float(current_.model.Weight(Channel(c))) != weights_[c])
            current_.model.SetWeight(Channel(c), weights_[c]);
    }
    renderer_.Upload(Layer::Picture, current_.picture, nullptr);
    UploadResult();
    UploadChannelMap();
}

void Viewer::SetWeight(Channel c, float w) {
    weights_[c] = w;
    if (!HasImage()) return;
    Stopwatch clock;
    current_.model.SetWeight(c, w);
    recombineMs_ = clock.LapMs();
    UploadResult();
}

void Viewer::UploadResult() {
    const Plane& total = current_.model.Total();
    renderer_.Upload(Layer::Heat, HeatImage(total), nullptr);

    const std::vector<char> edge = RegionEdge(current_.model.Top(), total.rows, total.cols);
    Plane line(total.rows, total.cols);
    for (std::size_t i = 0; i < edge.size(); ++i) line.v[i] = edge[i];
    // A one-pixel line fades away once the picture is scaled down to fit, so it is widened to three.
    Plane alpha = BoxFilter(line, 3, Border::Nearest);
    for (double& a : alpha.v) a = a > 0.0 ? 1.0 : 0.0;
    // Uploaded white and tinted when drawn, so the colour lives in one place.
    renderer_.Upload(Layer::Edge, GreyImage(alpha), &alpha);
}

void Viewer::UploadChannelMap() {
    if (!HasImage() || view_ < FIRST_CHANNEL_VIEW) return;
    const Channel c = Channel(view_ - FIRST_CHANNEL_VIEW);
    renderer_.Upload(Layer::ChannelMap, GreyImage(current_.model.ChannelMap(c)), nullptr);
}

void Viewer::Frame() {
    Collect();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("GazeProbe", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float panelWidth = 24.0f * ImGui::GetFontSize();
    ImGui::BeginChild("image", ImVec2(-panelWidth, 0.0f));
    DrawImage();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("panel");
    DrawPanel();
    ImGui::EndChild();
    ImGui::End();
}

void Viewer::DrawImage() {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 room = ImGui::GetContentRegionAvail();
    if (!HasImage()) {
        const char* hint =
            pending_.valid() ? "解析中…" : "画像をここにドロップするか、「開く…」から選んでください。";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + (room.x - size.x) / 2, origin.y + (room.y - size.y) / 2));
        ImGui::TextDisabled("%s", hint);
        return;
    }

    // Fit the picture into the space it has, centred, keeping its shape.
    const float rows = float(current_.picture.Rows()), cols = float(current_.picture.Cols());
    const float scale = std::min(room.x / cols, room.y / rows);
    const ImVec2 min(origin.x + (room.x - cols * scale) / 2, origin.y + (room.y - rows * scale) / 2);
    const ImVec2 max(min.x + cols * scale, min.y + rows * scale);
    auto at = [&](double x, double y) { return ImVec2(min.x + float(x) * scale, min.y + float(y) * scale); };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (view_ == OVERLAY_VIEW) {
        draw->AddImage(renderer_.Texture(Layer::Picture), min, max);
        draw->AddImage(renderer_.Texture(Layer::Heat), min, max, ImVec2(0, 0), ImVec2(1, 1),
                       IM_COL32(255, 255, 255, int(opacity_ * 255.0f)));
    } else if (view_ == TOTAL_VIEW) {
        draw->AddImage(renderer_.Texture(Layer::Heat), min, max);
    } else {
        draw->AddImage(renderer_.Texture(Layer::ChannelMap), min, max);
    }
    if (showRegion_)
        draw->AddImage(renderer_.Texture(Layer::Edge), min, max, ImVec2(0, 0), ImVec2(1, 1), MARK_COLOUR);
    if (showFaces_)
        for (const FaceBox& f : current_.faces)
            draw->AddRect(at(f.x, f.y), at(f.x + f.w, f.y + f.h), FACE_COLOUR, 0.0f, 0, 2.0f);
    if (showFixation_) {
        const FixationPoint& fix = current_.model.Fixation();
        const ImVec2 centre = at(fix.x * cols, fix.y * rows);
        const float arm = ImGui::GetFontSize();
        draw->AddLine(ImVec2(centre.x - arm, centre.y), ImVec2(centre.x + arm, centre.y), MARK_COLOUR, 3.0f);
        draw->AddLine(ImVec2(centre.x, centre.y - arm), ImVec2(centre.x, centre.y + arm), MARK_COLOUR, 3.0f);
    }
}

void Viewer::DrawPanel() {
    if (ImGui::Button("開く…")) {
        const std::wstring path = AskForImage(window_);
        if (!path.empty()) Open(path);
    }
    ImGui::SameLine();
    if (pending_.valid()) ImGui::TextDisabled("解析中…");
    else if (!current_.path.empty())
        ImGui::TextUnformatted(std::filesystem::path(current_.path).filename().u8string().c_str());
    if (!pending_.valid() && !current_.path.empty() && !HasImage())
        ImGui::TextColored(WARNING, "%s（%s）", ImageStatusText(current_.imageStatus),
                           current_.imageDetail.c_str());

    ImGui::SeparatorText("重み");
    ImGui::TextDisabled("0 にすると、そのチャンネルは完全に切れます。");
    for (int c = 0; c < ChannelCount; ++c) {
        // ImGui tells widgets apart by label, and the face slider shares its label with the face checkbox.
        ImGui::PushID(c);
        float w = weights_[c];
        if (ImGui::SliderFloat(ChannelLabel(Channel(c)), &w, 0.0f, 2.0f, "%.2f")) SetWeight(Channel(c), w);
        ImGui::SetItemTooltip("%s", ChannelHint(Channel(c)));
        ImGui::PopID();
    }
    if (ImGui::Button("初期値に戻す")) {
        const SaliencyModel defaults;
        for (int c = 0; c < ChannelCount; ++c) SetWeight(Channel(c), float(defaults.Weight(Channel(c))));
    }

    ImGui::SeparatorText("表示");
    const char* views[FIRST_CHANNEL_VIEW + ChannelCount] = {"絵に重ねる", "顕著性マップ"};
    for (int c = 0; c < ChannelCount; ++c) views[FIRST_CHANNEL_VIEW + c] = ChannelLabel(Channel(c));
    if (ImGui::Combo("画像", &view_, views, FIRST_CHANNEL_VIEW + ChannelCount)) UploadChannelMap();
    if (view_ == OVERLAY_VIEW) ImGui::SliderFloat("不透明度", &opacity_, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("上位領域", &showRegion_);
    ImGui::SetItemTooltip("顕著性がピークの 90%% 以上になる範囲。注視点は、この中で最も強い塊の重心です。");
    ImGui::SameLine();
    ImGui::Checkbox("注視点", &showFixation_);
    ImGui::SameLine();
    ImGui::Checkbox("顔", &showFaces_);

    if (!HasImage()) return;
    const SaliencyModel& model = current_.model;
    const FixationPoint& fix = model.Fixation();

    ImGui::SeparatorText("予測注視点");
    ImGui::Text("x %.3f  y %.3f（ピクセル %d, %d）", fix.x, fix.y, int(fix.x * current_.picture.Cols()),
                int(fix.y * current_.picture.Rows()));

    ImGui::SeparatorText("目を引く要因の内訳");
    if (ImGui::BeginTable("shares", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("チャンネル", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("画面全体");
        ImGui::TableSetupColumn("上位領域");
        ImGui::TableHeadersRow();
        for (int c = 0; c < ChannelCount; ++c) {
            char whole[16], top[16];
            std::snprintf(whole, sizeof whole, "%.1f%%", 100.0 * model.Share(Channel(c)));
            std::snprintf(top, sizeof top, "%.1f%%", 100.0 * model.TopShare(Channel(c)));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ChannelLabel(Channel(c)));
            ImGui::TableNextColumn();
            ImGui::ProgressBar(float(model.Share(Channel(c))), ImVec2(-FLT_MIN, 0.0f), whole);
            ImGui::TableNextColumn();
            ImGui::ProgressBar(float(model.TopShare(Channel(c))), ImVec2(-FLT_MIN, 0.0f), top);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("顔検出（イラスト用 YOLOv8）");
    if (current_.faceStatus != FaceStatus::Ready)
        ImGui::TextColored(WARNING, "利用できません — %s（%s）", FaceStatusText(current_.faceStatus),
                           current_.faceDetail.c_str());
    else if (current_.faces.empty())
        ImGui::TextUnformatted("この絵では検出されませんでした");
    else
        ImGui::Text("%d 件", int(current_.faces.size()));

    ImGui::SeparatorText("処理時間");
    ImGui::Text("読み込み %.0f ms　Oklab 変換 %.0f ms", current_.loadMs, current_.oklabMs);
    ImGui::Text("顔検出 %.0f ms　チャンネル計算 %.0f ms", current_.facesMs, current_.saliencyMs);
    if (recombineMs_ > 0.0) ImGui::Text("再合成 %.1f ms", recombineMs_);
}
