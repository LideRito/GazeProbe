#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "renderer.h"
#include "viewer.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

static const float FONT_SIZE = 16.0f;
static const double WINDOW_SHARE = 0.85;

static Renderer* renderer = nullptr;
static Viewer* viewer = nullptr;

static LRESULT WINAPI WindowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (renderer && wParam != SIZE_MINIMIZED) renderer->Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DROPFILES: {
        const HDROP drop = HDROP(wParam);
        std::wstring path(DragQueryFileW(drop, 0, nullptr, 0), L'\0');
        DragQueryFileW(drop, 0, path.data(), UINT(path.size() + 1));
        DragFinish(drop);
        if (viewer) viewer->Open(path);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, msg, wParam, lParam);
}

// A system font with Japanese and Chinese glyphs, so file names in either script show; else ImGui's own.
static void LoadFont(ImGuiIO& io) {
    wchar_t windows[MAX_PATH] = {};
    GetWindowsDirectoryW(windows, MAX_PATH);
    for (const wchar_t* name : {L"meiryo.ttc", L"YuGothM.ttc", L"msyh.ttc"}) {
        const std::filesystem::path font = std::filesystem::path(windows) / L"Fonts" / name;
        if (std::filesystem::exists(font) && io.Fonts->AddFontFromFileTTF(font.u8string().c_str())) return;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // The open dialog is a COM object, and a window's thread wants a single-threaded apartment.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_CLASSDC;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"GazeProbe";
    RegisterClassExW(&windowClass);
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = int((work.right - work.left) * WINDOW_SHARE);
    const int height = int((work.bottom - work.top) * WINDOW_SHARE);
    const HWND window = CreateWindowW(windowClass.lpszClassName, L"GazeProbe — 注視点の予測",
                                      WS_OVERLAPPEDWINDOW, work.left + (work.right - work.left - width) / 2,
                                      work.top + (work.bottom - work.top - height) / 2, width, height, nullptr,
                                      nullptr, instance, nullptr);
    DragAcceptFiles(window, TRUE);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(dpi);
    style.FontScaleDpi = dpi;
    style.FontSizeBase = FONT_SIZE;
    LoadFont(io);
    ImGui_ImplWin32_Init(window);

    int exitCode = 0;
    // The renderer and the viewer have to go before the ImGui context they draw into.
    {
        Renderer frameRenderer;
        if (!frameRenderer.Create(window)) {
            MessageBoxW(window, L"DirectX 11 を起動できませんでした。", L"GazeProbe", MB_ICONERROR);
            exitCode = 1;
        } else {
            Viewer frameViewer(frameRenderer, window);
            renderer = &frameRenderer;
            viewer = &frameViewer;
            ShowWindow(window, SW_SHOWDEFAULT);
            UpdateWindow(window);

            int argc = 0;
            wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argc > 1) frameViewer.Open(argv[1]);
            LocalFree(argv);

            bool done = false;
            while (!done) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    done = done || msg.message == WM_QUIT;
                }
                if (done) break;
                if (frameRenderer.Occluded()) {
                    Sleep(10);
                    continue;
                }
                frameRenderer.NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                frameViewer.Frame();
                ImGui::Render();
                frameRenderer.Draw();
            }
            renderer = nullptr;
            viewer = nullptr;
        }
    }

    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    CoUninitialize();
    return exitCode;
}
