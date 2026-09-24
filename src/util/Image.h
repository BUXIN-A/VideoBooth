#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vb {
namespace img {

// 统一使用 32 位预乘 BGRA（WIC: GUID_WICPixelFormat32bppPBGRA）自上而下存储，
// 与 GDI AlphaBlend、OpenGL (ONE, ONE_MINUS_SRC_ALPHA) 混合方式一致。
class Image {
public:
    bool LoadFromFile(const std::wstring& path);
    // 缩放解码：按最长边不超过 maxEdge 输出（相册缩略图用，避免解码出整幅原图）
    bool LoadThumbnail(const std::wstring& path, int maxEdge);
    void Reset();

    bool Valid() const { return width_ > 0 && height_ > 0 && !pixels_.empty(); }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    const uint8_t* pixels() const { return pixels_.empty() ? nullptr : pixels_.data(); }

private:
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;
    std::vector<uint8_t> pixels_;
};

// 以指定质量（1-100）保存 JPG，输入为预乘或非预乘的 32 位 BGRA 图像
bool SaveJpeg(const std::wstring& path, const uint8_t* bgra, int width, int height,
              int stride, int quality);

// 保存为 PNG（保留 alpha 通道），输入为 32 位预乘 BGRA 图像
bool SavePng(const std::wstring& path, const uint8_t* bgra, int width, int height, int stride);

} // namespace img
} // namespace vb
