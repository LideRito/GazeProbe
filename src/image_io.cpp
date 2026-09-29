#include "image_io.h"

#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "pixel_utils.h"

using Microsoft::WRL::ComPtr;

static std::string HexCode(HRESULT hr) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08lX", static_cast<unsigned long>(hr));
    return text;
}

// Straight (not premultiplied) alpha, composited over a white background.
static double OverWhite(BYTE colour, BYTE alpha) {
    const double a = alpha / 255.0;
    return colour / 255.0 * a + (1.0 - a);
}

static void FlattenBgra(std::vector<BYTE>& px) {
    for (std::size_t i = 0; i < px.size(); i += 4) {
        for (int c = 0; c < 3; ++c) px[i + c] = BYTE(ToLevel(OverWhite(px[i + c], px[i + 3])));
        px[i + 3] = 255;
    }
}

static RgbImage PlanesFromBgra(const std::vector<BYTE>& px, int w, int h) {
    RgbImage img{Plane(h, w), Plane(h, w), Plane(h, w)};
    for (std::size_t i = 0; i < img.r.Size(); ++i) {
        img.b.v[i] = OverWhite(px[i * 4 + 0], px[i * 4 + 3]);
        img.g.v[i] = OverWhite(px[i * 4 + 1], px[i * 4 + 3]);
        img.r.v[i] = OverWhite(px[i * 4 + 2], px[i * 4 + 3]);
    }
    return img;
}

static HRESULT CopyBgra(IWICBitmapSource* source, std::vector<BYTE>& px, int& w, int& h) {
    UINT uw = 0, uh = 0;
    const HRESULT hr = source->GetSize(&uw, &uh);
    if (FAILED(hr)) return hr;
    w = int(uw);
    h = int(uh);
    px.resize(std::size_t(w) * h * 4);
    return source->CopyPixels(nullptr, UINT(w) * 4, UINT(px.size()), px.data());
}

ImageCodec::ImageCodec() {
    comStarted_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&factory_));
    if (!factory_) status_ = ImageStatus::Unavailable;
}

ImageCodec::~ImageCodec() {
    if (factory_) factory_->Release();
    if (comStarted_) CoUninitialize();
}

RgbImage ImageCodec::Load(const std::wstring& path, int longEdge) {
    if (!factory_) return {};
    status_ = ImageStatus::Ready;
    detail_.clear();

    HRESULT hr = S_OK;
    auto fail = [this](ImageStatus status, HRESULT code) {
        status_ = status;
        detail_ = HexCode(code);
        return RgbImage();
    };

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return fail(ImageStatus::OpenFailed, hr);

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(hr = decoder->GetFrame(0, &frame))) return fail(ImageStatus::DecodeFailed, hr);

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(hr = factory_->CreateFormatConverter(&converter)))
        return fail(ImageStatus::DecodeFailed, hr);
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                               nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return fail(ImageStatus::DecodeFailed, hr);

    std::vector<BYTE> px;
    int w = 0, h = 0;
    if (FAILED(hr = CopyBgra(converter.Get(), px, w, h))) return fail(ImageStatus::DecodeFailed, hr);

    const double scale = double(longEdge) / std::max(w, h);
    if (scale < 1.0) {
        // Resizing before flattening would bleed the colour hidden under transparent pixels.
        FlattenBgra(px);
        ComPtr<IWICBitmap> bitmap;
        hr = factory_->CreateBitmapFromMemory(UINT(w), UINT(h), GUID_WICPixelFormat32bppBGRA,
                                              UINT(w) * 4, UINT(px.size()), px.data(), &bitmap);
        if (FAILED(hr)) return fail(ImageStatus::ResizeFailed, hr);

        const int nw = ScaledSize(w, scale);
        const int nh = ScaledSize(h, scale);
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(hr = factory_->CreateBitmapScaler(&scaler))) return fail(ImageStatus::ResizeFailed, hr);
        hr = scaler->Initialize(bitmap.Get(), UINT(nw), UINT(nh), WICBitmapInterpolationModeHighQualityCubic);
        if (FAILED(hr)) return fail(ImageStatus::ResizeFailed, hr);
        if (FAILED(hr = CopyBgra(scaler.Get(), px, w, h))) return fail(ImageStatus::ResizeFailed, hr);
    }

    return PlanesFromBgra(px, w, h);
}

bool ImageCodec::SavePng(const std::wstring& path, const RgbImage& img) {
    if (!factory_) return false;
    status_ = ImageStatus::Ready;
    detail_.clear();
    const int w = img.Cols(), h = img.Rows();
    std::vector<BYTE> px(std::size_t(w) * h * 3);
    for (std::size_t i = 0; i < img.r.Size(); ++i) {
        px[i * 3 + 0] = BYTE(ToLevel(img.b.v[i]));
        px[i * 3 + 1] = BYTE(ToLevel(img.g.v[i]));
        px[i * 3 + 2] = BYTE(ToLevel(img.r.v[i]));
    }

    HRESULT hr = S_OK;
    auto fail = [this](HRESULT code) {
        status_ = ImageStatus::WriteFailed;
        detail_ = HexCode(code);
        return false;
    };

    ComPtr<IWICStream> stream;
    if (FAILED(hr = factory_->CreateStream(&stream))) return fail(hr);
    if (FAILED(hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
        return fail(hr);

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(hr = factory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)))
        return fail(hr);
    if (FAILED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        return fail(hr);

    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(hr = encoder->CreateNewFrame(&frame, nullptr))) return fail(hr);
    if (FAILED(hr = frame->Initialize(nullptr))) return fail(hr);
    if (FAILED(hr = frame->SetSize(UINT(w), UINT(h)))) return fail(hr);
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(hr = frame->SetPixelFormat(&format))) return fail(hr);
    if (format != GUID_WICPixelFormat24bppBGR) {
        status_ = ImageStatus::WriteFailed;
        detail_ = "encoder refused 24-bit BGR";
        return false;
    }
    hr = frame->WritePixels(UINT(h), UINT(w) * 3, UINT(px.size()), px.data());
    if (FAILED(hr)) return fail(hr);
    if (FAILED(hr = frame->Commit())) return fail(hr);
    if (FAILED(hr = encoder->Commit())) return fail(hr);
    return true;
}
