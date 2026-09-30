#include "ui/OverlayCanvas.h"

#include "util/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace vb {
namespace ui {
namespace {

// 全局 GUI 字体族，留空表示使用内置默认字体
std::wstring& GlobalFontFamily() {
    static std::wstring family;
    return family;
}

HBITMAP CreateTopDownDib(HDC dc, int width, int height, void** bits, int* stride) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // 负高度表示自上而下
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap == nullptr) {
        return nullptr;
    }
    if (bits != nullptr) {
        *bits = pixels;
    }
    if (stride != nullptr) {
        *stride = width * 4;
    }
    return bitmap;
}

inline float ChannelFloat(BYTE value) {
    return static_cast<float>(value) / 255.0f;
}

inline void BlendPremultiplied(uint8_t* dst, float red, float green, float blue, float alpha) {
    const float inverse = 1.0f - alpha;
    const int r = static_cast<int>(red * alpha * 255.0f + dst[2] * inverse + 0.5f);
    const int g = static_cast<int>(green * alpha * 255.0f + dst[1] * inverse + 0.5f);
    const int b = static_cast<int>(blue * alpha * 255.0f + dst[0] * inverse + 0.5f);
    dst[0] = static_cast<uint8_t>(std::min(b, 255));
    dst[1] = static_cast<uint8_t>(std::min(g, 255));
    dst[2] = static_cast<uint8_t>(std::min(r, 255));
    dst[3] = static_cast<uint8_t>(
        std::min(static_cast<int>(alpha * 255.0f + dst[3] * inverse + 0.5f), 255));
}

// 预乘源按通道混合：dst = src * scale + dst * (1 - alpha * scale)
// 入参为 0-255 区间的预乘分量
inline void BlendPremultipliedFloats(uint8_t* dst, float srcBlue, float srcGreen, float srcRed,
                                     float srcAlpha, float scale) {
    const float alpha = (srcAlpha / 255.0f) * scale;
    const float inverse = 1.0f - alpha;
    const int b = static_cast<int>(srcBlue * scale + dst[0] * inverse + 0.5f);
    const int g = static_cast<int>(srcGreen * scale + dst[1] * inverse + 0.5f);
    const int r = static_cast<int>(srcRed * scale + dst[2] * inverse + 0.5f);
    const int a = static_cast<int>(alpha * 255.0f + dst[3] * inverse + 0.5f);
    dst[0] = static_cast<uint8_t>(std::min(b, 255));
    dst[1] = static_cast<uint8_t>(std::min(g, 255));
    dst[2] = static_cast<uint8_t>(std::min(r, 255));
    dst[3] = static_cast<uint8_t>(std::min(a, 255));
}

} // namespace

OverlayCanvas::~OverlayCanvas() {
    Release();
}

bool OverlayCanvas::Resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (valid() && width_ == width && height_ == height) {
        return true;
    }

    if (dc_ == nullptr) {
        dc_ = ::CreateCompatibleDC(nullptr);
        if (dc_ == nullptr) {
            VB_ERROR("创建离屏画布设备上下文失败");
            return false;
        }
    }
    if (bitmap_ != nullptr) {
        ::SelectObject(dc_, previousBitmap_);
        ::DeleteObject(bitmap_);
        bitmap_ = nullptr;
        previousBitmap_ = nullptr;
        bits_ = nullptr;
    }

    bitmap_ = CreateTopDownDib(dc_, width, height, reinterpret_cast<void**>(&bits_), &stride_);
    if (bitmap_ == nullptr) {
        VB_ERROR("创建离屏画布位图失败 (%dx%d)", width, height);
        width_ = 0;
        height_ = 0;
        stride_ = 0;
        return false;
    }
    previousBitmap_ = ::SelectObject(dc_, bitmap_);
    width_ = width;
    height_ = height;
    std::memset(bits_, 0, static_cast<size_t>(stride_) * static_cast<size_t>(height_));
    return true;
}

void OverlayCanvas::Release() {
    if (dc_ != nullptr) {
        if (bitmap_ != nullptr) {
            ::SelectObject(dc_, previousBitmap_);
            ::DeleteObject(bitmap_);
            bitmap_ = nullptr;
            previousBitmap_ = nullptr;
            bits_ = nullptr;
        }
        ::DeleteDC(dc_);
        dc_ = nullptr;
    }
    if (maskDc_ != nullptr) {
        if (maskBitmap_ != nullptr) {
            ::SelectObject(maskDc_, maskPreviousBitmap_);
            ::DeleteObject(maskBitmap_);
            maskBitmap_ = nullptr;
            maskPreviousBitmap_ = nullptr;
            maskBits_ = nullptr;
        }
        ::DeleteDC(maskDc_);
        maskDc_ = nullptr;
    }
    for (const auto& entry : fonts_) {
        ::DeleteObject(entry.second);
    }
    fonts_.clear();
    width_ = 0;
    height_ = 0;
    stride_ = 0;
    maskWidth_ = 0;
    maskHeight_ = 0;
    maskStride_ = 0;
}

void OverlayCanvas::Clear() {
    clipEnabled_ = false;
    if (valid()) {
        std::memset(bits_, 0, static_cast<size_t>(stride_) * static_cast<size_t>(height_));
    }
}

void OverlayCanvas::SetClipRect(const RECT& rect) {
    clip_ = rect;
    clipEnabled_ = true;
}

void OverlayCanvas::ResetClip() {
    clipEnabled_ = false;
}

void OverlayCanvas::FillSpan(int x0, int x1, int y, COLORREF color, BYTE alpha) {
    if (!valid() || y < 0 || y >= height_) {
        return;
    }
    if (clipEnabled_ && (y < clip_.top || y >= clip_.bottom)) {
        return;
    }
    x0 = std::max(x0, 0);
    x1 = std::min(x1, width_ - 1);
    if (clipEnabled_) {
        x0 = std::max(x0, static_cast<int>(clip_.left));
        x1 = std::min(x1, static_cast<int>(clip_.right) - 1);
    }
    if (x1 < x0) {
        return;
    }
    const float a = ChannelFloat(alpha);
    const float r = ChannelFloat(GetRValue(color));
    const float g = ChannelFloat(GetGValue(color));
    const float b = ChannelFloat(GetBValue(color));
    uint8_t* row = At(x0, y);
    for (int x = x0; x <= x1; ++x) {
        BlendPremultiplied(row + (x - x0) * 4, r, g, b, a);
    }
}

void OverlayCanvas::FillRect(const RECT& rect, COLORREF color, BYTE alpha) {
    if (!valid()) {
        return;
    }
    const int y0 = std::max(static_cast<int>(rect.top), 0);
    const int y1 = std::min(static_cast<int>(rect.bottom) - 1, height_ - 1);
    for (int y = y0; y <= y1; ++y) {
        FillSpan(rect.left, rect.right - 1, y, color, alpha);
    }
}

void OverlayCanvas::FillRoundRect(const RECT& rect, int radius, COLORREF color, BYTE alpha) {
    if (!valid()) {
        return;
    }
    const int left = static_cast<int>(rect.left);
    const int right = static_cast<int>(rect.right) - 1;
    const int top = static_cast<int>(rect.top);
    const int bottom = static_cast<int>(rect.bottom) - 1;
    if (right < left || bottom < top) {
        return;
    }
    const int maxRadius = std::min((right - left + 1) / 2, (bottom - top + 1) / 2);
    radius = std::max(0, std::min(radius, maxRadius));
    if (radius == 0) {
        for (int y = top; y <= bottom; ++y) {
            FillSpan(left, right, y, color, alpha);
        }
        return;
    }

    const float r = ChannelFloat(GetRValue(color));
    const float g = ChannelFloat(GetGValue(color));
    const float b = ChannelFloat(GetBValue(color));
    const float baseAlpha = ChannelFloat(alpha);

    // 圆角按有符号距离计算逐像素覆盖度，得到平滑边缘（抗锯齿）
    const float centerX = static_cast<float>(left + right + 1) * 0.5f;
    const float centerY = static_cast<float>(top + bottom + 1) * 0.5f;
    const float cornerX = static_cast<float>(right - left + 1) * 0.5f - static_cast<float>(radius);
    const float cornerY = static_cast<float>(bottom - top + 1) * 0.5f - static_cast<float>(radius);

    const int y0 = std::max(top, 0);
    const int y1 = std::min(bottom, height_ - 1);
    for (int y = y0; y <= y1; ++y) {
        if (clipEnabled_ && (y < clip_.top || y >= clip_.bottom)) {
            continue;
        }
        const float dy =
            std::max(std::fabs(static_cast<float>(y) + 0.5f - centerY) - cornerY, 0.0f);
        int x0 = std::max(left, 0);
        int x1 = std::min(right, width_ - 1);
        if (clipEnabled_) {
            x0 = std::max(x0, static_cast<int>(clip_.left));
            x1 = std::min(x1, static_cast<int>(clip_.right) - 1);
        }
        if (x1 < x0) {
            continue;
        }
        uint8_t* row = bits_ + static_cast<ptrdiff_t>(y) * stride_;
        for (int x = x0; x <= x1; ++x) {
            const float dx =
                std::max(std::fabs(static_cast<float>(x) + 0.5f - centerX) - cornerX, 0.0f);
            const float distance = (dx > 0.0f && dy > 0.0f)
                                       ? std::sqrt(dx * dx + dy * dy) - static_cast<float>(radius)
                                       : std::max(dx, dy) - static_cast<float>(radius);
            const float coverage = std::min(1.0f, std::max(0.0f, 0.5f - distance));
            if (coverage <= 0.0f) {
                continue;
            }
            BlendPremultiplied(row + static_cast<ptrdiff_t>(x) * 4, r, g, b,
                               baseAlpha * coverage);
        }
    }
}

void OverlayCanvas::DrawPixels(const uint8_t* bgra, int sourceWidth, int sourceHeight,
                               int sourceStride, const RECT& dest, int rotateQuarter,
                               BYTE alpha) {
    if (!valid() || bgra == nullptr || sourceWidth <= 0 || sourceHeight <= 0) {
        return;
    }
    if (sourceStride <= 0) {
        sourceStride = sourceWidth * 4;
    }
    rotateQuarter = ((rotateQuarter % 4) + 4) % 4;

    const int destWidth = static_cast<int>(dest.right - dest.left);
    const int destHeight = static_cast<int>(dest.bottom - dest.top);
    if (destWidth <= 0 || destHeight <= 0) {
        return;
    }

    const float globalAlpha = ChannelFloat(alpha);
    const float inverseWidth = 1.0f / static_cast<float>(destWidth);
    const float inverseHeight = 1.0f / static_cast<float>(destHeight);
    const int destLeft = static_cast<int>(dest.left);
    const int destTop = static_cast<int>(dest.top);
    // 预先收敛到画布内的像素范围，避免逐像素做边界判断
    const int xStart = std::max(0, -destLeft);
    const int xEnd = std::min(destWidth, width_ - destLeft);
    const int yStart = std::max(0, -destTop);
    const int yEnd = std::min(destHeight, height_ - destTop);
    for (int y = yStart; y < yEnd; ++y) {
        const int canvasY = destTop + y;
        if (clipEnabled_ && (canvasY < clip_.top || canvasY >= clip_.bottom)) {
            continue;
        }
        uint8_t* canvasRow = bits_ + static_cast<ptrdiff_t>(canvasY) * stride_;
        const float v0 = static_cast<float>(y) * inverseHeight;
        const float v1 = static_cast<float>(y + 1) * inverseHeight;
        for (int x = xStart; x < xEnd; ++x) {
            const int canvasX = destLeft + x;
            if (clipEnabled_ && (canvasX < clip_.left || canvasX >= clip_.right)) {
                continue;
            }
            const float u0 = static_cast<float>(x) * inverseWidth;
            const float u1 = static_cast<float>(x + 1) * inverseWidth;

            // 目的像素在源图中的覆盖区域（旋转后仍为轴对齐矩形）
            float sx0 = 0.0f;
            float sx1 = 0.0f;
            float sy0 = 0.0f;
            float sy1 = 0.0f;
            switch (rotateQuarter) {
            case 1:
                sx0 = v0 * sourceWidth;
                sx1 = v1 * sourceWidth;
                sy0 = (1.0f - u1) * sourceHeight;
                sy1 = (1.0f - u0) * sourceHeight;
                break;
            case 2:
                sx0 = (1.0f - u1) * sourceWidth;
                sx1 = (1.0f - u0) * sourceWidth;
                sy0 = (1.0f - v1) * sourceHeight;
                sy1 = (1.0f - v0) * sourceHeight;
                break;
            case 3:
                sx0 = (1.0f - v1) * sourceWidth;
                sx1 = (1.0f - v0) * sourceWidth;
                sy0 = u0 * sourceHeight;
                sy1 = u1 * sourceHeight;
                break;
            default:
                sx0 = u0 * sourceWidth;
                sx1 = u1 * sourceWidth;
                sy0 = v0 * sourceHeight;
                sy1 = v1 * sourceHeight;
                break;
            }

            int x0 = static_cast<int>(std::floor(sx0));
            int x1 = static_cast<int>(std::ceil(sx1));
            int y0 = static_cast<int>(std::floor(sy0));
            int y1 = static_cast<int>(std::ceil(sy1));
            x0 = std::max(x0, 0);
            y0 = std::max(y0, 0);
            x1 = std::min(std::max(x1, x0 + 1), sourceWidth);
            y1 = std::min(std::max(y1, y0 + 1), sourceHeight);

            float sampleB = 0.0f;
            float sampleG = 0.0f;
            float sampleR = 0.0f;
            float sampleA = 0.0f;
            if (sx1 - sx0 <= 1.0f && sy1 - sy0 <= 1.0f) {
                // 放大：双线性插值，避免图标在 GUI 缩放后出现块状锯齿
                const float fx = (sx0 + sx1) * 0.5f - 0.5f;
                const float fy = (sy0 + sy1) * 0.5f - 0.5f;
                const int ix = std::max(0, std::min(static_cast<int>(std::floor(fx)),
                                                   sourceWidth - 1));
                const int iy = std::max(0, std::min(static_cast<int>(std::floor(fy)),
                                                   sourceHeight - 1));
                const int jx = std::min(ix + 1, sourceWidth - 1);
                const int jy = std::min(iy + 1, sourceHeight - 1);
                const float tx = std::max(0.0f, std::min(fx - static_cast<float>(ix), 1.0f));
                const float ty = std::max(0.0f, std::min(fy - static_cast<float>(iy), 1.0f));
                const uint8_t* rowA = bgra + static_cast<ptrdiff_t>(iy) * sourceStride;
                const uint8_t* rowB = bgra + static_cast<ptrdiff_t>(jy) * sourceStride;
                const uint8_t* p00 = rowA + static_cast<ptrdiff_t>(ix) * 4;
                const uint8_t* p10 = rowA + static_cast<ptrdiff_t>(jx) * 4;
                const uint8_t* p01 = rowB + static_cast<ptrdiff_t>(ix) * 4;
                const uint8_t* p11 = rowB + static_cast<ptrdiff_t>(jx) * 4;
                const float w00 = (1.0f - tx) * (1.0f - ty);
                const float w10 = tx * (1.0f - ty);
                const float w01 = (1.0f - tx) * ty;
                const float w11 = tx * ty;
                sampleB = p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11;
                sampleG = p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11;
                sampleR = p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11;
                sampleA = p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11;
            } else {
                // 缩小：区域均值采样，兼顾缩放质量与性能
                const int stepX = std::max(1, (x1 - x0) / 3);
                const int stepY = std::max(1, (y1 - y0) / 3);
                float sumB = 0.0f;
                float sumG = 0.0f;
                float sumR = 0.0f;
                float sumA = 0.0f;
                int samples = 0;
                for (int sy = y0; sy < y1; sy += stepY) {
                    const uint8_t* row = bgra + static_cast<ptrdiff_t>(sy) * sourceStride;
                    for (int sx = x0; sx < x1; sx += stepX) {
                        const uint8_t* pixel = row + static_cast<ptrdiff_t>(sx) * 4;
                        sumB += pixel[0];
                        sumG += pixel[1];
                        sumR += pixel[2];
                        sumA += pixel[3];
                        ++samples;
                    }
                }
                if (samples == 0) {
                    continue;
                }
                const float inverse = 1.0f / static_cast<float>(samples);
                sampleB = sumB * inverse;
                sampleG = sumG * inverse;
                sampleR = sumR * inverse;
                sampleA = sumA * inverse;
            }
            // 预乘数据按通道均值/插值后可直接混合
            BlendPremultipliedFloats(canvasRow + static_cast<ptrdiff_t>(canvasX) * 4, sampleB,
                                     sampleG, sampleR, sampleA, globalAlpha);
        }
    }
}

void OverlayCanvas::DrawDashedRect(const RECT& rect, COLORREF color, BYTE alpha, int thickness,
                                   int dashLength, int gapLength) {
    if (!valid() || thickness <= 0) {
        return;
    }
    dashLength = std::max(1, dashLength);
    gapLength = std::max(1, gapLength);

    const int left = static_cast<int>(rect.left);
    const int top = static_cast<int>(rect.top);
    const int right = static_cast<int>(rect.right) - 1;
    const int bottom = static_cast<int>(rect.bottom) - 1;
    if (right <= left || bottom <= top) {
        return;
    }

    const auto dashLine = [&](int x0, int y0, int x1, int y1) {
        const int dx = x1 - x0;
        const int dy = y1 - y0;
        const int steps = std::max(std::abs(dx), std::abs(dy));
        for (int i = 0; i <= steps; ++i) {
            const int phase = i % (dashLength + gapLength);
            if (phase >= dashLength) {
                continue;
            }
            const int x = x0 + (dx == 0 ? 0 : dx * i / steps);
            const int y = y0 + (dy == 0 ? 0 : dy * i / steps);
            RECT dot = {x, y, x + thickness, y + thickness};
            FillRect(dot, color, alpha);
        }
    };

    dashLine(left, top, right, top);
    dashLine(left, bottom, right, bottom);
    dashLine(left, top, left, bottom);
    dashLine(right, top, right, bottom);
}

bool OverlayCanvas::EnsureMask(int width, int height) {
    if (maskDc_ == nullptr) {
        maskDc_ = ::CreateCompatibleDC(nullptr);
        if (maskDc_ == nullptr) {
            return false;
        }
    }
    if (maskBitmap_ != nullptr && maskWidth_ >= width && maskHeight_ >= height) {
        return true;
    }
    if (maskBitmap_ != nullptr) {
        ::SelectObject(maskDc_, maskPreviousBitmap_);
        ::DeleteObject(maskBitmap_);
        maskBitmap_ = nullptr;
        maskBits_ = nullptr;
    }
    maskWidth_ = std::max(width, maskWidth_);
    maskHeight_ = std::max(height, maskHeight_);
    maskBitmap_ = CreateTopDownDib(maskDc_, maskWidth_, maskHeight_,
                                   reinterpret_cast<void**>(&maskBits_), &maskStride_);
    if (maskBitmap_ == nullptr) {
        maskWidth_ = 0;
        maskHeight_ = 0;
        maskStride_ = 0;
        return false;
    }
    maskPreviousBitmap_ = ::SelectObject(maskDc_, maskBitmap_);
    return true;
}

void OverlayCanvas::SetFontFamily(const std::wstring& family) {
    GlobalFontFamily() = family;
}

const std::wstring& OverlayCanvas::FontFamily() {
    return GlobalFontFamily();
}

HFONT OverlayCanvas::FontFor(int fontPixels) {
    fontPixels = std::max(6, fontPixels);
    const std::wstring& family = GlobalFontFamily();
    if (fontsFamily_ != family) {
        // 字体族变化：丢弃旧句柄，避免缓存继续使用旧字体
        for (const auto& entry : fonts_) {
            ::DeleteObject(entry.second);
        }
        fonts_.clear();
        fontsFamily_ = family;
    }
    const auto existing = fonts_.find(fontPixels);
    if (existing != fonts_.end()) {
        return existing->second;
    }
    const wchar_t* face = family.empty() ? L"Microsoft YaHei UI" : family.c_str();
    HFONT font = ::CreateFontW(-fontPixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    if (font == nullptr) {
        font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    }
    fonts_.emplace(fontPixels, font);
    return font;
}

SIZE OverlayCanvas::MeasureText(const std::wstring& text, int fontPixels, UINT format) {
    SIZE size = {};
    if (text.empty()) {
        return size;
    }
    HDC dc = dc_ != nullptr ? dc_ : ::GetDC(nullptr);
    HFONT font = FontFor(fontPixels);
    HGDIOBJ previous = ::SelectObject(dc, font);
    RECT bounds = {0, 0, 0, 0};
    ::DrawTextW(dc, text.c_str(), -1, &bounds, format | DT_CALCRECT);
    size.cx = bounds.right - bounds.left;
    size.cy = bounds.bottom - bounds.top;
    ::SelectObject(dc, previous);
    if (dc_ == nullptr) {
        ::ReleaseDC(nullptr, dc);
    }
    return size;
}

void OverlayCanvas::DrawText(const std::wstring& text, const RECT& box, int fontPixels,
                             COLORREF color, BYTE alpha, UINT format) {
    if (!valid() || text.empty()) {
        return;
    }
    const int boxWidth = static_cast<int>(box.right - box.left);
    const int boxHeight = static_cast<int>(box.bottom - box.top);
    if (boxWidth <= 0 || boxHeight <= 0) {
        return;
    }
    if (!EnsureMask(boxWidth, boxHeight)) {
        return;
    }

    // 1) 用 GDI 在蒙版上绘制白色文字，通道值即覆盖度
    const RECT maskRect = {0, 0, boxWidth, boxHeight};
    ::SetBkMode(maskDc_, OPAQUE);
    ::SetBkColor(maskDc_, RGB(0, 0, 0));
    ::SetTextColor(maskDc_, RGB(255, 255, 255));
    ::FillRect(maskDc_, &maskRect, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
    HFONT font = FontFor(fontPixels);
    HGDIOBJ previousFont = ::SelectObject(maskDc_, font);
    RECT textRect = maskRect;
    ::DrawTextW(maskDc_, text.c_str(), -1, &textRect, format);
    ::SelectObject(maskDc_, previousFont);

    // 2) 按覆盖度合成到画布
    const float r = ChannelFloat(GetRValue(color));
    const float g = ChannelFloat(GetGValue(color));
    const float b = ChannelFloat(GetBValue(color));
    const float baseAlpha = ChannelFloat(alpha);
    // 预先收敛到画布内的采样范围，避免逐像素做边界判断
    const int boxLeft = static_cast<int>(box.left);
    const int boxTop = static_cast<int>(box.top);
    const int xStart = std::max(0, -boxLeft);
    const int xEnd = std::min(boxWidth, width_ - boxLeft);
    const int yStart = std::max(0, -boxTop);
    const int yEnd = std::min(boxHeight, height_ - boxTop);
    for (int y = yStart; y < yEnd; ++y) {
        const int canvasY = boxTop + y;
        if (clipEnabled_ && (canvasY < clip_.top || canvasY >= clip_.bottom)) {
            continue;
        }
        const uint8_t* maskRow = maskBits_ + static_cast<ptrdiff_t>(y) * maskStride_;
        uint8_t* canvasRow = bits_ + static_cast<ptrdiff_t>(canvasY) * stride_;
        for (int x = xStart; x < xEnd; ++x) {
            const int canvasX = boxLeft + x;
            if (clipEnabled_ && (canvasX < clip_.left || canvasX >= clip_.right)) {
                continue;
            }
            const uint8_t coverage = maskRow[x * 4];
            if (coverage == 0) {
                continue;
            }
            BlendPremultiplied(canvasRow + static_cast<ptrdiff_t>(canvasX) * 4, r, g, b,
                               baseAlpha * ChannelFloat(coverage));
        }
    }
}

void OverlayCanvas::ApplyImageEffect(const RECT& rect, float sharpen, float brightness) {
    if (!valid() || (sharpen <= 0.0f && brightness == 1.0f)) {
        return;
    }
    const int left = std::max(0, static_cast<int>(rect.left));
    const int top = std::max(0, static_cast<int>(rect.top));
    const int right = std::min(width_, static_cast<int>(rect.right));
    const int bottom = std::min(height_, static_cast<int>(rect.bottom));
    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0) {
        return;
    }

    // 先拷贝源区域，避免边计算边修改导致锐化结果沿扫描方向扩散
    const size_t rowBytes = static_cast<size_t>(width) * 4u;
    std::vector<uint8_t> source(rowBytes * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        std::memcpy(source.data() + rowBytes * static_cast<size_t>(y),
                    bits_ + static_cast<ptrdiff_t>(top + y) * stride_ +
                        static_cast<ptrdiff_t>(left) * 4,
                    rowBytes);
    }

    const auto sample = [&source, width](int x, int y, int channel) {
        return static_cast<float>(
            source[(static_cast<size_t>(y) * width + x) * 4u + static_cast<size_t>(channel)]);
    };

    for (int y = 0; y < height; ++y) {
        uint8_t* row = bits_ + static_cast<ptrdiff_t>(top + y) * stride_ +
                       static_cast<ptrdiff_t>(left) * 4;
        for (int x = 0; x < width; ++x) {
            const int xm = x > 0 ? x - 1 : x;
            const int xp = x + 1 < width ? x + 1 : x;
            const int ym = y > 0 ? y - 1 : y;
            const int yp = y + 1 < height ? y + 1 : y;
            uint8_t* pixel = row + static_cast<ptrdiff_t>(x) * 4;
            for (int channel = 0; channel < 3; ++channel) {
                float value = sample(x, y, channel);
                if (sharpen > 0.0f) {
                    const float neighbors = sample(xm, y, channel) + sample(xp, y, channel) +
                                            sample(x, ym, channel) + sample(x, yp, channel);
                    value += (value - neighbors * 0.25f) * sharpen;
                }
                value *= brightness;
                value = std::min(value, static_cast<float>(pixel[3]));
                pixel[channel] = static_cast<uint8_t>(std::max(0.0f, value) + 0.5f);
            }
        }
    }
}

} // namespace ui
} // namespace vb
