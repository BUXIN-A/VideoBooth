#include "util/Image.h"

#include "util/Log.h"

#include "util/Strings.h"

#include <wincodec.h>

#include <wrl/client.h>

#include <algorithm>

namespace vb {
namespace img {
namespace {

using Microsoft::WRL::ComPtr;

// WIC 工厂按需创建并缓存（COM 已由调用方初始化）。
// 界面线程与后台保存线程都会调用，故用函数级静态变量保证只初始化一次，
// 避免并发首次调用时互相覆盖导致工厂被提前释放。
IWICImagingFactory* ImagingFactory() {
    static IWICImagingFactory* factory = []() -> IWICImagingFactory* {
        IWICImagingFactory* created = nullptr;
        const HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                             CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&created));
        if (FAILED(hr)) {
            VB_ERROR("创建 WIC 工厂失败: %ls", HresultToWide(hr).c_str());
            return nullptr;
        }
        return created;
    }();
    return factory;
}

// 打开图片文件并取第一帧（加载与缩略图共用同一套解码器创建流程）
bool OpenFirstFrame(IWICImagingFactory* factory, const std::wstring& path,
                    ComPtr<IWICBitmapDecoder>& decoder, ComPtr<IWICBitmapFrameDecode>& frame) {
    const HRESULT hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnDemand,
                                                         decoder.GetAddressOf());
    if (FAILED(hr)) {
        VB_WARN("解码图片失败（创建解码器）: %ls, %ls", path.c_str(),
                HresultToWide(hr).c_str());
        return false;
    }
    return SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf()));
}

} // namespace

bool Image::LoadFromFile(const std::wstring& path) {
    Reset();

    IWICImagingFactory* factory = ImagingFactory();
    if (factory == nullptr) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!OpenFirstFrame(factory, path, decoder, frame)) {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        VB_WARN("图片格式转换失败: %ls, %ls", path.c_str(), HresultToWide(hr).c_str());
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        return false;
    }

    width_ = static_cast<int>(width);
    height_ = static_cast<int>(height);
    stride_ = width_ * 4;
    pixels_.resize(static_cast<size_t>(stride_) * static_cast<size_t>(height_));

    hr = converter->CopyPixels(nullptr, static_cast<UINT>(stride_),
                               static_cast<UINT>(pixels_.size()), pixels_.data());
    if (FAILED(hr)) {
        VB_WARN("图片像素拷贝失败: %ls, %ls", path.c_str(), HresultToWide(hr).c_str());
        Reset();
        return false;
    }

    VB_INFO("加载图片成功: %ls (%dx%d)", path.c_str(), width_, height_);
    return true;
}

bool Image::LoadThumbnail(const std::wstring& path, int maxEdge) {
    Reset();
    if (maxEdge <= 0) {
        return false;
    }

    IWICImagingFactory* factory = ImagingFactory();
    if (factory == nullptr) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!OpenFirstFrame(factory, path, decoder, frame)) {
        return false;
    }

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    HRESULT hr = frame->GetSize(&sourceWidth, &sourceHeight);
    if (FAILED(hr) || sourceWidth == 0 || sourceHeight == 0) {
        return false;
    }

    // 按最长边等比缩放；原图本身较小时保持原尺寸
    const UINT longest = std::max(sourceWidth, sourceHeight);
    UINT targetWidth = sourceWidth;
    UINT targetHeight = sourceHeight;
    if (longest > static_cast<UINT>(maxEdge)) {
        const double ratio = static_cast<double>(maxEdge) / static_cast<double>(longest);
        targetWidth = std::max<UINT>(1, static_cast<UINT>(sourceWidth * ratio + 0.5));
        targetHeight = std::max<UINT>(1, static_cast<UINT>(sourceHeight * ratio + 0.5));
    }

    // 先缩放再转换格式，减少像素格式转换的数据量
    ComPtr<IWICBitmapSource> source;
    if (targetWidth != sourceWidth || targetHeight != sourceHeight) {
        ComPtr<IWICBitmapScaler> scaler;
        hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
        hr = scaler->Initialize(frame.Get(), targetWidth, targetHeight,
                                WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            VB_WARN("缩略图缩放失败: %ls, %ls", path.c_str(), HresultToWide(hr).c_str());
            return false;
        }
        source = scaler.Get();
    } else {
        source = frame.Get();
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return false;
    }

    width_ = static_cast<int>(targetWidth);
    height_ = static_cast<int>(targetHeight);
    stride_ = width_ * 4;
    pixels_.resize(static_cast<size_t>(stride_) * static_cast<size_t>(height_));

    hr = converter->CopyPixels(nullptr, static_cast<UINT>(stride_),
                               static_cast<UINT>(pixels_.size()), pixels_.data());
    if (FAILED(hr)) {
        VB_WARN("缩略图像素拷贝失败: %ls, %ls", path.c_str(), HresultToWide(hr).c_str());
        Reset();
        return false;
    }
    return true;
}

void Image::Reset() {
    width_ = 0;
    height_ = 0;
    stride_ = 0;
    pixels_.clear();
}

bool SaveJpeg(const std::wstring& path, const uint8_t* bgra, int width, int height,
              int stride, int quality) {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    IWICImagingFactory* factory = ImagingFactory();
    if (factory == nullptr) {
        return false;
    }

    ComPtr<IWICStream> stream;
    HRESULT hr = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        VB_ERROR("创建照片文件失败: %ls, %ls", path.c_str(), HresultToWide(hr).c_str());
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr,
                                encoder.GetAddressOf());
    if (FAILED(hr)) {
        VB_ERROR("创建 JPEG 编码器失败: %ls", HresultToWide(hr).c_str());
        return false;
    }
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frameEncode;
    ComPtr<IPropertyBag2> properties;
    hr = encoder->CreateNewFrame(frameEncode.GetAddressOf(), properties.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    if (properties) {
        PROPBAG2 option = {};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        ::VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = static_cast<float>(quality) / 100.0f;
        properties->Write(1, &option, &value);
        ::VariantClear(&value);
    }

    hr = frameEncode->Initialize(properties.Get());
    if (FAILED(hr)) {
        return false;
    }
    hr = frameEncode->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    if (FAILED(hr)) {
        return false;
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frameEncode->SetPixelFormat(&format);
    if (FAILED(hr)) {
        return false;
    }
    // JPEG 不支持 alpha，若编码器将格式规整为 24bppBGR，则需要去 alpha 的连续缓冲
    if (!::IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        std::vector<uint8_t> packed(static_cast<size_t>(width) * 3u *
                                   static_cast<size_t>(height));
        for (int y = 0; y < height; ++y) {
            const uint8_t* src = bgra + static_cast<ptrdiff_t>(y) * stride;
            uint8_t* dst = packed.data() + static_cast<size_t>(y) * width * 3u;
            for (int x = 0; x < width; ++x) {
                dst[x * 3 + 0] = src[x * 4 + 0];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
            }
        }
        hr = frameEncode->WritePixels(static_cast<UINT>(height),
                                      static_cast<UINT>(width) * 3u,
                                      static_cast<UINT>(packed.size()), packed.data());
    } else {
        hr = frameEncode->WritePixels(static_cast<UINT>(height),
                                      static_cast<UINT>(stride),
                                      static_cast<UINT>(stride) * static_cast<UINT>(height),
                                      const_cast<BYTE*>(bgra));
    }
    if (FAILED(hr)) {
        VB_ERROR("写入 JPEG 像素失败: %ls", HresultToWide(hr).c_str());
        return false;
    }

    hr = frameEncode->Commit();
    if (FAILED(hr)) {
        return false;
    }
    hr = encoder->Commit();
    if (FAILED(hr)) {
        VB_ERROR("提交 JPEG 编码失败: %ls", HresultToWide(hr).c_str());
        return false;
    }
    return true;
}

bool SavePng(const std::wstring& path, const uint8_t* bgra, int width, int height, int stride) {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (stride <= 0) {
        stride = width * 4;
    }
    IWICImagingFactory* factory = ImagingFactory();
    if (factory == nullptr) {
        return false;
    }

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.GetAddressOf()))) {
        return false;
    }
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        VB_ERROR("创建 PNG 文件失败: %ls", path.c_str());
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                      encoder.GetAddressOf()))) {
        return false;
    }
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frameEncode;
    ComPtr<IPropertyBag2> properties;
    if (FAILED(encoder->CreateNewFrame(frameEncode.GetAddressOf(), properties.GetAddressOf()))) {
        return false;
    }
    if (FAILED(frameEncode->Initialize(properties.Get()))) {
        return false;
    }
    if (FAILED(frameEncode->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)))) {
        return false;
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(frameEncode->SetPixelFormat(&format))) {
        return false;
    }
    HRESULT hr = E_FAIL;
    if (::IsEqualGUID(format, GUID_WICPixelFormat32bppPBGRA)) {
        hr = frameEncode->WritePixels(static_cast<UINT>(height), static_cast<UINT>(stride),
                                      static_cast<UINT>(stride) * static_cast<UINT>(height),
                                      const_cast<BYTE*>(bgra));
    } else if (::IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        // 编码器拒绝预乘格式：逐像素反预乘后按非预乘 BGRA 写入
        const size_t rowBytes = static_cast<size_t>(width) * 4u;
        std::vector<uint8_t> straight(rowBytes * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y) {
            const uint8_t* source = bgra + static_cast<ptrdiff_t>(y) * stride;
            uint8_t* destination = straight.data() + rowBytes * static_cast<size_t>(y);
            for (int x = 0; x < width; ++x) {
                const uint8_t alpha = source[x * 4 + 3];
                destination[x * 4 + 3] = alpha;
                if (alpha == 0) {
                    destination[x * 4 + 0] = 0;
                    destination[x * 4 + 1] = 0;
                    destination[x * 4 + 2] = 0;
                    continue;
                }
                for (int c = 0; c < 3; ++c) {
                    const int value =
                        (static_cast<int>(source[x * 4 + c]) * 255 + alpha / 2) / alpha;
                    destination[x * 4 + c] = static_cast<uint8_t>(std::min(255, value));
                }
            }
        }
        hr = frameEncode->WritePixels(static_cast<UINT>(height),
                                      static_cast<UINT>(rowBytes),
                                      static_cast<UINT>(straight.size()), straight.data());
    } else {
        VB_ERROR("PNG 编码器不支持的像素格式，无法写入: %ls", path.c_str());
        return false;
    }
    if (FAILED(hr)) {
        VB_ERROR("写入 PNG 像素失败: %ls, %ls", path.c_str(), HresultToWide(hr).c_str());
        return false;
    }
    if (FAILED(frameEncode->Commit())) {
        return false;
    }
    hr = encoder->Commit();
    if (FAILED(hr)) {
        VB_ERROR("提交 PNG 编码失败: %ls", HresultToWide(hr).c_str());
        return false;
    }
    return true;
}

} // namespace img
} // namespace vb
