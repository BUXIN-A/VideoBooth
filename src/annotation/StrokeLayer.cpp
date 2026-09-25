#include "annotation/StrokeLayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vb {
namespace annotation {
namespace {

inline float ChannelOf(uint32_t argb, int shift) {
    return static_cast<float>((argb >> shift) & 0xFFu) / 255.0f;
}

} // namespace

void StrokeLayer::Reset(int width, int height) {
    if (width <= 0 || height <= 0) {
        pixels_.clear();
        width_ = 0;
        height_ = 0;
        stride_ = 0;
        hasInk_ = false;
        pointerActive_ = false;
        dirtyValid_ = false;
        return;
    }
    if (width_ == width && height_ == height) {
        return;
    }
    width_ = width;
    height_ = height;
    stride_ = width_ * 4;
    pixels_.assign(static_cast<size_t>(stride_) * static_cast<size_t>(height_), 0);
    hasInk_ = false;
    pointerActive_ = false;
    InvalidateAll();
    ++version_;
}

bool StrokeLayer::ImportPixels(const uint8_t* bgra, int width, int height, int stride) {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (stride <= 0) {
        stride = width * 4;
    }
    Reset(width, height);
    if (!valid() || width_ != width || height_ != height) {
        return false;
    }
    bool hasInk = false;
    for (int y = 0; y < height_; ++y) {
        const uint8_t* source = bgra + static_cast<ptrdiff_t>(y) * stride;
        uint8_t* destination = pixels_.data() + static_cast<ptrdiff_t>(y) * stride_;
        std::memcpy(destination, source, static_cast<size_t>(width_) * 4u);
        if (!hasInk) {
            for (int x = 0; x < width_; ++x) {
                if (destination[x * 4 + 3] != 0) {
                    hasInk = true;
                    break;
                }
            }
        }
    }
    hasInk_ = hasInk;
    pointerActive_ = false;
    dirtyValid_ = false;
    InvalidateAll();
    ++version_;
    return true;
}

void StrokeLayer::Clear() {
    if (!valid()) {
        return;
    }
    std::fill(pixels_.begin(), pixels_.end(), static_cast<uint8_t>(0));
    hasInk_ = false;
    pointerActive_ = false;
    InvalidateAll();
    ++version_;
}

void StrokeLayer::SetThickness(int pixels) {
    thickness_ = std::max(1, pixels);
}

void StrokeLayer::SetEraserRadius(float radius) {
    eraserRadius_ = std::max(2.0f, radius);
}

void StrokeLayer::InvalidateAll() {
    if (!valid()) {
        dirtyValid_ = false;
        return;
    }
    dirtyLeft_ = 0;
    dirtyTop_ = 0;
    dirtyRight_ = width_;
    dirtyBottom_ = height_;
    dirtyValid_ = true;
}

void StrokeLayer::ExpandDirty(int left, int top, int right, int bottom) {
    left = std::max(0, left);
    top = std::max(0, top);
    right = std::min(width_, right);
    bottom = std::min(height_, bottom);
    if (right <= left || bottom <= top) {
        return;
    }
    if (!dirtyValid_) {
        dirtyLeft_ = left;
        dirtyTop_ = top;
        dirtyRight_ = right;
        dirtyBottom_ = bottom;
        dirtyValid_ = true;
        return;
    }
    dirtyLeft_ = std::min(dirtyLeft_, left);
    dirtyTop_ = std::min(dirtyTop_, top);
    dirtyRight_ = std::max(dirtyRight_, right);
    dirtyBottom_ = std::max(dirtyBottom_, bottom);
}

bool StrokeLayer::TakeDirtyRect(RECT& out) {
    if (!dirtyValid_ || !valid()) {
        return false;
    }
    out.left = dirtyLeft_;
    out.top = dirtyTop_;
    out.right = dirtyRight_;
    out.bottom = dirtyBottom_;
    dirtyValid_ = false;
    return out.right > out.left && out.bottom > out.top;
}

void StrokeLayer::PointerDown(float imageX, float imageY, bool erase) {
    if (!valid()) {
        return;
    }
    pointerActive_ = true;
    erasing_ = erase;
    lastX_ = imageX;
    lastY_ = imageY;
    const float radius = erase ? eraserRadius_ : static_cast<float>(thickness_) * 0.5f;
    Stamp(imageX, imageY, radius, erase);
}

void StrokeLayer::PointerMove(float imageX, float imageY) {
    if (!valid() || !pointerActive_) {
        return;
    }
    const float radius = erasing_ ? eraserRadius_ : static_cast<float>(thickness_) * 0.5f;
    StampLine(lastX_, lastY_, imageX, imageY, radius, erasing_);
    lastX_ = imageX;
    lastY_ = imageY;
}

void StrokeLayer::PointerUp() {
    pointerActive_ = false;
    erasing_ = false;
}

void StrokeLayer::StampLine(float x0, float y0, float x1, float y1, float radius, bool erase) {
    const float deltaX = x1 - x0;
    const float deltaY = y1 - y0;
    const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (distance <= 0.0001f) {
        // 原地未移动：不重复盖章，避免同一位置反复叠加
        return;
    }
    // 间距按笔宽取，但必须保证每段都至少盖一次章：缓慢书写时两次移动的距离
    // 常常小于间距，若按整段丢弃会出现笔迹断断续续（快速书写时距离大才正常）
    const float step = std::max(1.0f, radius * 0.4f);
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / step)));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        Stamp(x0 + deltaX * t, y0 + deltaY * t, radius, erase);
    }
}

void StrokeLayer::Stamp(float x, float y, float radius, bool erase) {
    if (!valid()) {
        return;
    }
    if (erase) {
        EraseCircle(x, y, radius);
    } else {
        BlendCircle(x, y, radius);
        hasInk_ = true;
    }
    ++version_;
}

void StrokeLayer::BlendCircle(float centerX, float centerY, float radius) {
    const float alpha = ChannelOf(color_, 24);
    // 颜色分量预先放大到 0-255，避免在内层逐像素重复乘法
    const float red = ChannelOf(color_, 16) * 255.0f;
    const float green = ChannelOf(color_, 8) * 255.0f;
    const float blue = ChannelOf(color_, 0) * 255.0f;

    const int left = static_cast<int>(std::floor(centerX - radius - 1.0f));
    const int top = static_cast<int>(std::floor(centerY - radius - 1.0f));
    const int right = static_cast<int>(std::ceil(centerX + radius + 1.0f));
    const int bottom = static_cast<int>(std::ceil(centerY + radius + 1.0f));

    const int xStart = std::max(0, left);
    const int xEnd = std::min(width_, right);
    const int yStart = std::max(0, top);
    const int yEnd = std::min(height_, bottom);
    const float outer = radius + 0.5f;
    const float outerSquared = outer * outer;

    for (int y = yStart; y < yEnd; ++y) {
        uint8_t* row = pixels_.data() + static_cast<ptrdiff_t>(y) * stride_;
        const float deltaY = static_cast<float>(y) + 0.5f - centerY;
        const float deltaYSquared = deltaY * deltaY;
        for (int x = xStart; x < xEnd; ++x) {
            const float deltaX = static_cast<float>(x) + 0.5f - centerX;
            const float distanceSquared = deltaX * deltaX + deltaYSquared;
            if (distanceSquared >= outerSquared) {
                continue; // 圆外像素直接跳过，省去开方
            }
            const float sourceAlpha = alpha * std::min(1.0f, outer - std::sqrt(distanceSquared));
            const float inverse = 1.0f - sourceAlpha;
            uint8_t* pixel = row + static_cast<ptrdiff_t>(x) * 4;
            pixel[0] = static_cast<uint8_t>(
                std::min(255.0f, blue * sourceAlpha + pixel[0] * inverse + 0.5f));
            pixel[1] = static_cast<uint8_t>(
                std::min(255.0f, green * sourceAlpha + pixel[1] * inverse + 0.5f));
            pixel[2] = static_cast<uint8_t>(
                std::min(255.0f, red * sourceAlpha + pixel[2] * inverse + 0.5f));
            pixel[3] = static_cast<uint8_t>(
                std::min(255.0f, sourceAlpha * 255.0f + pixel[3] * inverse + 0.5f));
        }
    }

    ExpandDirty(left, top, right + 1, bottom + 1);
}

void StrokeLayer::EraseCircle(float centerX, float centerY, float radius) {
    const int left = static_cast<int>(std::floor(centerX - radius - 1.0f));
    const int top = static_cast<int>(std::floor(centerY - radius - 1.0f));
    const int right = static_cast<int>(std::ceil(centerX + radius + 1.0f));
    const int bottom = static_cast<int>(std::ceil(centerY + radius + 1.0f));

    const int xStart = std::max(0, left);
    const int xEnd = std::min(width_, right);
    const int yStart = std::max(0, top);
    const int yEnd = std::min(height_, bottom);
    const float outer = radius + 0.5f;
    const float outerSquared = outer * outer;

    for (int y = yStart; y < yEnd; ++y) {
        uint8_t* row = pixels_.data() + static_cast<ptrdiff_t>(y) * stride_;
        const float deltaY = static_cast<float>(y) + 0.5f - centerY;
        const float deltaYSquared = deltaY * deltaY;
        for (int x = xStart; x < xEnd; ++x) {
            const float deltaX = static_cast<float>(x) + 0.5f - centerX;
            const float distanceSquared = deltaX * deltaX + deltaYSquared;
            if (distanceSquared >= outerSquared) {
                continue; // 圆外像素直接跳过，省去开方
            }
            const float keep = 1.0f - std::min(1.0f, outer - std::sqrt(distanceSquared));
            uint8_t* pixel = row + static_cast<ptrdiff_t>(x) * 4;
            pixel[0] = static_cast<uint8_t>(pixel[0] * keep + 0.5f);
            pixel[1] = static_cast<uint8_t>(pixel[1] * keep + 0.5f);
            pixel[2] = static_cast<uint8_t>(pixel[2] * keep + 0.5f);
            pixel[3] = static_cast<uint8_t>(pixel[3] * keep + 0.5f);
        }
    }

    ExpandDirty(left, top, right + 1, bottom + 1);
}

void StrokeLayer::Composite(uint8_t* target, int targetStride) const {
    if (!valid() || target == nullptr || targetStride <= 0) {
        return;
    }
    constexpr float kInverse255 = 1.0f / 255.0f;
    for (int y = 0; y < height_; ++y) {
        const uint8_t* source = pixels_.data() + static_cast<ptrdiff_t>(y) * stride_;
        uint8_t* destination = target + static_cast<ptrdiff_t>(y) * targetStride;
        for (int x = 0; x < width_; ++x) {
            const uint8_t sourceAlpha = source[x * 4 + 3];
            if (sourceAlpha == 0) {
                continue;
            }
            const float inverse = 1.0f - static_cast<float>(sourceAlpha) * kInverse255;
            uint8_t* pixel = destination + static_cast<ptrdiff_t>(x) * 4;
            pixel[0] = static_cast<uint8_t>(
                std::min(255.0f, source[x * 4 + 0] + pixel[0] * inverse + 0.5f));
            pixel[1] = static_cast<uint8_t>(
                std::min(255.0f, source[x * 4 + 1] + pixel[1] * inverse + 0.5f));
            pixel[2] = static_cast<uint8_t>(
                std::min(255.0f, source[x * 4 + 2] + pixel[2] * inverse + 0.5f));
        }
    }
}

} // namespace annotation
} // namespace vb
