#include "app_utils.h"

#define NOMINMAX
#include <windows.h>

#include <filesystem>

static const wchar_t* const MODEL_NAME = L"anime_face_v14s.onnx";

double Stopwatch::LapMs() {
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - last_).count();
    last_ = now;
    return ms;
}

// The build copies the model into models/ beside the executable.
std::wstring DefaultModelPath() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return (std::filesystem::path(exe).parent_path() / L"models" / MODEL_NAME).wstring();
}
