#include "image_io.h"

#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "pixel_utils.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

static std::string Failure(const char* what, HRESULT hr) {
    char text[96];
    std::snprintf(text, sizeof text, "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
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
}

ImageCodec::~ImageCodec() {
    if (factory_) factory_->Release();
    if (comStarted_) CoUninitialize();
}

bool ImageCodec::Load(const std::wstring& path, int longEdge, RgbImage& out, std::string& error) {
    if (!factory_) {
        error = "WIC is not available";
        return false;
    }

    HRESULT hr = S_OK;
    auto fail = [&](const char* what) {
        error = Failure(what, hr);
        return false;
    };

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return fail("opening the image");

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(hr = decoder->GetFrame(0, &frame))) return fail("reading a frame");

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(hr = factory_->CreateFormatConverter(&converter))) return fail("creating a converter");
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                               nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return fail("converting to BGRA");

    std::vector<BYTE> px;
    int w = 0, h = 0;
    if (FAILED(hr = CopyBgra(converter.Get(), px, w, h))) return fail("copying pixels");

    const double scale = double(longEdge) / std::max(w, h);
    if (scale < 1.0) {
        // Resizing before flattening would bleed the colour hidden under transparent pixels.
        FlattenBgra(px);
        ComPtr<IWICBitmap> bitmap;
        hr = factory_->CreateBitmapFromMemory(UINT(w), UINT(h), GUID_WICPixelFormat32bppBGRA,
                                              UINT(w) * 4, UINT(px.size()), px.data(), &bitmap);
        if (FAILED(hr)) return fail("wrapping the pixels");

        const int nw = ScaledSize(w, scale);
        const int nh = ScaledSize(h, scale);
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(hr = factory_->CreateBitmapScaler(&scaler))) return fail("creating a scaler");
        hr = scaler->Initialize(bitmap.Get(), UINT(nw), UINT(nh), WICBitmapInterpolationModeHighQualityCubic);
        if (FAILED(hr)) return fail("setting up the resize");
        if (FAILED(hr = CopyBgra(scaler.Get(), px, w, h))) return fail("resizing");
    }

    out = PlanesFromBgra(px, w, h);
    return true;
}

bool ImageCodec::SavePng(const std::wstring& path, const RgbImage& img, std::string& error) {
    if (!factory_) {
        error = "WIC is not available";
        return false;
    }
    const int w = img.Cols(), h = img.Rows();
    std::vector<BYTE> px(std::size_t(w) * h * 3);
    for (std::size_t i = 0; i < img.r.Size(); ++i) {
        px[i * 3 + 0] = BYTE(ToLevel(img.b.v[i]));
        px[i * 3 + 1] = BYTE(ToLevel(img.g.v[i]));
        px[i * 3 + 2] = BYTE(ToLevel(img.r.v[i]));
    }

    HRESULT hr = S_OK;
    auto fail = [&](const char* what) {
        error = Failure(what, hr);
        return false;
    };

    ComPtr<IWICStream> stream;
    if (FAILED(hr = factory_->CreateStream(&stream))) return fail("creating a stream");
    if (FAILED(hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
        return fail("creating the file");

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(hr = factory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)))
        return fail("creating the encoder");
    if (FAILED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        return fail("starting the encoder");

    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(hr = encoder->CreateNewFrame(&frame, nullptr))) return fail("adding a frame");
    if (FAILED(hr = frame->Initialize(nullptr))) return fail("starting the frame");
    if (FAILED(hr = frame->SetSize(UINT(w), UINT(h)))) return fail("setting the size");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(hr = frame->SetPixelFormat(&format))) return fail("setting the pixel format");
    if (format != GUID_WICPixelFormat24bppBGR) {
        error = "the PNG encoder refused 24-bit BGR";
        return false;
    }
    hr = frame->WritePixels(UINT(h), UINT(w) * 3, UINT(px.size()), px.data());
    if (FAILED(hr)) return fail("writing pixels");
    if (FAILED(hr = frame->Commit())) return fail("finishing the frame");
    if (FAILED(hr = encoder->Commit())) return fail("finishing the file");
    return true;
}
