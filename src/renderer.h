#pragma once

#include <vector>

#include "image_io.h"
#include "imgui.h"

// HWND is a pointer to this opaque struct; naming it keeps <windows.h> out of the header.
struct HWND__;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

// The pictures the viewer draws, each kept as one texture.
enum class Layer { Picture, Heat, Edge, ChannelMap, Count };

// Owns the DirectX 11 device, the window's swap chain and one texture per layer, and draws ImGui into them.
class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Create(HWND__* window);
    void Resize(int width, int height);
    bool Occluded();
    void NewFrame();
    void Draw();

    // An alpha plane in [0, 1] makes the texture see-through where it is low; without one it is opaque.
    void Upload(Layer layer, const RgbImage& img, const Plane* alpha);
    ImTextureID Texture(Layer layer) const;

private:
    void CreateTarget();
    void ReleaseTarget();
    void ReleaseRetired();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* target_ = nullptr;
    ID3D11ShaderResourceView* layers_[int(Layer::Count)] = {};
    std::vector<ID3D11ShaderResourceView*> retired_;
    int pendingWidth_ = 0;
    int pendingHeight_ = 0;
    bool occluded_ = false;
    bool imguiReady_ = false;
};
