#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

#include "util/Image.h"

namespace vb {
namespace album {

// 相册中的一张照片：元数据 + 懒加载缩略图
struct PhotoEntry {
    std::wstring path;
    std::wstring name;
    ULONGLONG capturedAt = 0; // 文件写入时间，用于排序
    bool thumbnailTried = false;
    bool thumbnailReady = false;
    img::Image thumbnail;
};

// 临时照片目录的相册数据源：扫描列表、缩略图缓存、删除、另存为。
// 列表只加载低分辨率缩略图，展示时才由调用方加载原图。
class PhotoLibrary {
public:
    // 缩略图解码的最长边（像素）
    static constexpr int kThumbnailMaxEdge = 256;

    void SetDirectory(const std::wstring& directory) { directory_ = directory; }
    const std::wstring& directory() const { return directory_; }

    // 重新扫描目录，按拍摄时间倒序排列（最新在前），并释放旧缓存
    void Refresh();
    // 释放缩略图缓存（退出相册时调用）
    void Release();

    size_t size() const { return photos_.size(); }
    bool empty() const { return photos_.empty(); }
    const PhotoEntry& at(size_t index) const { return photos_[index]; }

    // 懒加载缩略图，失败返回 nullptr（记录已尝试，避免反复解码坏文件）
    const img::Image* Thumbnail(size_t index);

    // 删除临时文件并从列表移除
    bool Remove(size_t index);
    // 另存为：复制原图到目标路径（临时文件保留）
    bool Export(size_t index, const std::wstring& targetPath) const;

private:
    std::wstring directory_;
    std::vector<PhotoEntry> photos_;
};

} // namespace album
} // namespace vb
