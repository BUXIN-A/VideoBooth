#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace vb {
namespace annotation {

// 批注层：与全景画面同尺寸的预乘 BGRA 位图。
// 笔迹绘制与像素擦除均在图像坐标系完成，因此旋转/缩放/拖动时随画面一起变换。
class StrokeLayer {
public:
    void Reset(int width, int height);
    void Clear();

    // 用外部 32 位预乘 BGRA 位图整体替换本层（用于恢复某张图片的笔迹）
    bool ImportPixels(const uint8_t* bgra, int width, int height, int stride);

    bool valid() const { return !pixels_.empty() && width_ > 0 && height_ > 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    const uint8_t* pixels() const { return pixels_.empty() ? nullptr : pixels_.data(); }
    bool empty() const { return !hasInk_; }
    uint64_t version() const { return version_; }

    void SetColor(uint32_t argb) { color_ = argb; }
    uint32_t color() const { return color_; }
    void SetThickness(int pixels);
    int thickness() const { return thickness_; }
    void SetEraserRadius(float radius);
    float eraserRadius() const { return eraserRadius_; }

    // 指针交互（图像坐标）
    void PointerDown(float imageX, float imageY, bool erase);
    void PointerMove(float imageX, float imageY);
    void PointerUp();
    bool pointerActive() const { return pointerActive_; }

    // 取走脏矩形（用于局部纹理上传；层刚重建/清空时为整层）
    bool TakeDirtyRect(RECT& out);

    // 把批注合成到全景画面像素上（预乘 over）
    void Composite(uint8_t* target, int targetStride) const;

private:
    void Stamp(float x, float y, float radius, bool erase);
    void StampLine(float x0, float y0, float x1, float y1, float radius, bool erase);
    void BlendCircle(float centerX, float centerY, float radius);
    void EraseCircle(float centerX, float centerY, float radius);
    void ExpandDirty(int left, int top, int right, int bottom);
    void InvalidateAll();

    std::vector<uint8_t> pixels_;
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;

    uint32_t color_ = 0xFFFF3B30; // 默认红色
    int thickness_ = 5;           // 默认 5
    float eraserRadius_ = 20.0f;

    bool pointerActive_ = false;
    bool erasing_ = false;
    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
    bool hasInk_ = false;
    uint64_t version_ = 0;

    bool dirtyValid_ = false;
    int dirtyLeft_ = 0;
    int dirtyTop_ = 0;
    int dirtyRight_ = 0;
    int dirtyBottom_ = 0;
};

} // namespace annotation
} // namespace vb
