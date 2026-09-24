#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

namespace vb {
namespace ui {

// GDI 离屏画布：32 位预乘 BGRA 自上而下 DIB，
// 直接按预乘 alpha 混合写入内存，绘制结果作为 OpenGL 纹理叠加到画面上
class OverlayCanvas {
public:
    OverlayCanvas() = default;
    ~OverlayCanvas();

    OverlayCanvas(const OverlayCanvas&) = delete;
    OverlayCanvas& operator=(const OverlayCanvas&) = delete;

    bool Resize(int width, int height);
    void Release();

    bool valid() const { return bits_ != nullptr && width_ > 0 && height_ > 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    const uint8_t* pixels() const { return bits_; }
    // 供分层窗口 UpdateLayeredWindow 直接使用（32 位预乘 BGRA）
    HDC deviceContext() const { return dc_; }

    void Clear();

    // 裁剪区域：仅绘制落在该矩形内的内容（相册滚动视口等），Clear 会重置
    void SetClipRect(const RECT& rect);
    void ResetClip();

    void FillRect(const RECT& rect, COLORREF color, BYTE alpha = 255);
    void FillRoundRect(const RECT& rect, int radius, COLORREF color, BYTE alpha = 255);

    // 绘制 32 位预乘 BGRA 位图，rotateQuarter 为顺时针 90° 次数
    void DrawPixels(const uint8_t* bgra, int sourceWidth, int sourceHeight, int sourceStride,
                    const RECT& dest, int rotateQuarter = 0, BYTE alpha = 255);

    void DrawDashedRect(const RECT& rect, COLORREF color, BYTE alpha = 255, int thickness = 2,
                        int dashLength = 12, int gapLength = 8);

    void DrawText(const std::wstring& text, const RECT& box, int fontPixels, COLORREF color,
                  BYTE alpha = 255,
                  UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SIZE MeasureText(const std::wstring& text, int fontPixels, UINT format = DT_SINGLELINE);

private:
    bool EnsureMask(int width, int height);
    HFONT FontFor(int fontPixels);
    uint8_t* At(int x, int y) { return bits_ + static_cast<ptrdiff_t>(y) * stride_ + x * 4; }
    void BlendPixel(int x, int y, float red, float green, float blue, float alpha);
    void FillSpan(int x0, int x1, int y, COLORREF color, BYTE alpha);

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    uint8_t* bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;

    bool clipEnabled_ = false;
    RECT clip_ = {0, 0, 0, 0};

    HDC maskDc_ = nullptr;
    HBITMAP maskBitmap_ = nullptr;
    HGDIOBJ maskPreviousBitmap_ = nullptr;
    uint8_t* maskBits_ = nullptr;
    int maskWidth_ = 0;
    int maskHeight_ = 0;
    int maskStride_ = 0;

    std::map<int, HFONT> fonts_;
};

} // namespace ui
} // namespace vb
