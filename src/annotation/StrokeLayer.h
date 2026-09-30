#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace vb {
namespace annotation {

// 批注层：逻辑尺寸与全景画面一致，内部按 2 倍超采样保存预乘 BGRA 位图，
// 放大画面或导出合成时笔迹边缘更平滑（像素过多时自动退回 1 倍，避免占用过大内存）。
// 笔迹绘制与像素擦除均在图像坐标系完成，因此旋转/缩放/拖动时随画面一起变换。
class StrokeLayer {
public:
    void Reset(int width, int height);
    void Clear();

    // 以逻辑尺寸的预乘 BGRA 位图整体替换本层（用于兼容 1 倍笔迹文件）
    bool ImportPixels(const uint8_t* bgra, int width, int height, int stride);
    // 直接把已按超采样尺寸保存的位图写入内部缓冲（尺寸须与 bufferWidth/Height 一致）
    bool ImportBuffer(const uint8_t* bgra, int stride);

    bool valid() const { return !pixels_.empty() && width_ > 0 && height_ > 0; }
    // 逻辑（画面）尺寸
    int width() const { return width_; }
    int height() const { return height_; }
    bool matches(int width, int height) const {
        return valid() && width_ == width && height_ == height;
    }

    // 内部超采样缓冲
    int supersample() const { return supersample_; }
    int bufferWidth() const { return bufferWidth_; }
    int bufferHeight() const { return bufferHeight_; }
    int bufferStride() const { return bufferStride_; }
    const uint8_t* bufferPixels() const { return pixels_.empty() ? nullptr : pixels_.data(); }

    bool empty() const { return !hasInk_; }
    uint64_t version() const { return version_; }

    void SetColor(uint32_t argb) { color_ = argb; }
    uint32_t color() const { return color_; }
    void SetThickness(int pixels);
    int thickness() const { return thickness_; }
    void SetEraserRadius(float radius);
    float eraserRadius() const { return eraserRadius_; }

    // 指针交互（逻辑图像坐标）
    void PointerDown(float imageX, float imageY, bool erase);
    void PointerMove(float imageX, float imageY);
    void PointerUp();
    bool pointerActive() const { return pointerActive_; }

    // 取走脏矩形（内部缓冲坐标；层刚重建/清空时为整层）
    bool TakeDirtyRect(RECT& out);

    // 把批注合成到逻辑尺寸的全景画面像素上（预乘 over，超采样缓冲自动降采样）
    void Composite(uint8_t* target, int targetStride) const;

private:
    void Allocate(int width, int height);
    void Stamp(float x, float y, float radius, bool erase);
    void StampLine(float x0, float y0, float x1, float y1, float radius, bool erase);
    void BlendCircle(float centerX, float centerY, float radius);
    void EraseCircle(float centerX, float centerY, float radius);
    void ExpandDirty(int left, int top, int right, int bottom);
    void InvalidateAll();

    std::vector<uint8_t> pixels_; // 超采样缓冲
    int width_ = 0;               // 逻辑宽
    int height_ = 0;              // 逻辑高
    int supersample_ = 1;
    int bufferWidth_ = 0;
    int bufferHeight_ = 0;
    int bufferStride_ = 0;

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
