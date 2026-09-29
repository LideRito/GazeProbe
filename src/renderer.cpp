#include "renderer.h"

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

#include "imgui_impl_dx11.h"
#include "pixel_utils.h"

using Microsoft::WRL::ComPtr;

static const float BACKGROUND[4] = {0.09f, 0.09f, 0.11f, 1.0f};

Renderer::~Renderer() {
    if (imguiReady_) ImGui_ImplDX11_Shutdown();
    for (ID3D11ShaderResourceView* layer : layers_)
        if (layer) layer->Release();
    ReleaseRetired();
    ReleaseTarget();
    if (swapChain_) swapChain_->Release();
    if (context_) context_->Release();
    if (device_) device_->Release();
}

bool Renderer::Create(HWND__* window) {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    auto create = [&](D3D_DRIVER_TYPE driver) {
        return D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc,
                                             &swapChain_, &device_, nullptr, &context_);
    };
    HRESULT hr = create(D3D_DRIVER_TYPE_HARDWARE);
    // WARP is Microsoft's software rasteriser, for machines without a usable GPU.
    if (hr == DXGI_ERROR_UNSUPPORTED) hr = create(D3D_DRIVER_TYPE_WARP);
    if (FAILED(hr)) return false;

    CreateTarget();
    imguiReady_ = ImGui_ImplDX11_Init(device_, context_);
    return imguiReady_;
}

void Renderer::Resize(int width, int height) {
    pendingWidth_ = width;
    pendingHeight_ = height;
}

// A minimised window has nothing to show, so the frame loop can rest until it comes back.
bool Renderer::Occluded() {
    if (occluded_ && swapChain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) return true;
    occluded_ = false;
    return false;
}

void Renderer::NewFrame() {
    if (pendingWidth_ > 0 && pendingHeight_ > 0) {
        ReleaseTarget();
        swapChain_->ResizeBuffers(0, UINT(pendingWidth_), UINT(pendingHeight_), DXGI_FORMAT_UNKNOWN, 0);
        CreateTarget();
        pendingWidth_ = pendingHeight_ = 0;
    }
    ImGui_ImplDX11_NewFrame();
}

void Renderer::Draw() {
    context_->OMSetRenderTargets(1, &target_, nullptr);
    context_->ClearRenderTargetView(target_, BACKGROUND);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    occluded_ = swapChain_->Present(1, 0) == DXGI_STATUS_OCCLUDED;
    ReleaseRetired();
}

void Renderer::Upload(Layer layer, const RgbImage& img, const Plane* alpha) {
    ID3D11ShaderResourceView*& slot = layers_[int(layer)];
    // The old texture may already be in this frame's draw list, so it is freed only once the frame is drawn.
    if (slot) retired_.push_back(slot);
    slot = nullptr;
    if (img.Empty()) return;

    // A texture takes interleaved RGBA bytes, row after row.
    std::vector<std::uint8_t> px(img.r.Size() * 4);
    for (std::size_t i = 0; i < img.r.Size(); ++i) {
        px[i * 4 + 0] = std::uint8_t(ToLevel(img.r.v[i]));
        px[i * 4 + 1] = std::uint8_t(ToLevel(img.g.v[i]));
        px[i * 4 + 2] = std::uint8_t(ToLevel(img.b.v[i]));
        px[i * 4 + 3] = alpha ? std::uint8_t(ToLevel(alpha->v[i])) : 255;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = UINT(img.Cols());
    desc.Height = UINT(img.Rows());
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data = {px.data(), UINT(img.Cols()) * 4, 0};

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device_->CreateTexture2D(&desc, &data, &texture))) return;
    device_->CreateShaderResourceView(texture.Get(), nullptr, &slot);
}

ImTextureID Renderer::Texture(Layer layer) const {
    return ImTextureID(reinterpret_cast<std::uintptr_t>(layers_[int(layer)]));
}

void Renderer::CreateTarget() {
    ComPtr<ID3D11Texture2D> back;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&back));
    device_->CreateRenderTargetView(back.Get(), nullptr, &target_);
}

void Renderer::ReleaseTarget() {
    if (target_) target_->Release();
    target_ = nullptr;
}

void Renderer::ReleaseRetired() {
    for (ID3D11ShaderResourceView* old : retired_) old->Release();
    retired_.clear();
}
