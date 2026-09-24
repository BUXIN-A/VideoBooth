#include "album/PhotoLibrary.h"

#include "core/Paths.h"
#include "util/Log.h"

#include <algorithm>

namespace vb {
namespace album {
namespace {

constexpr wchar_t kPhotoPattern[] = L"IMG_*.jpg";

// FILETIME（UTC，100ns）→ 可比较的 64 位值
ULONGLONG FileTimeValue(const FILETIME& time) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

} // namespace

void PhotoLibrary::Refresh() {
    photos_.clear();
    if (directory_.empty()) {
        VB_WARN("相册目录为空，无法扫描照片");
        return;
    }

    const std::wstring pattern = paths::JoinPath(directory_, kPhotoPattern);
    WIN32_FIND_DATAW data = {};
    HANDLE find = ::FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        VB_INFO("相册目录暂无照片: %ls", directory_.c_str());
        return;
    }

    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        PhotoEntry entry;
        entry.name = data.cFileName;
        entry.path = paths::JoinPath(directory_, entry.name);
        entry.capturedAt = FileTimeValue(data.ftLastWriteTime);
        photos_.push_back(std::move(entry));
    } while (::FindNextFileW(find, &data));
    ::FindClose(find);

    // 最新拍摄的排在最前；时间相同时按文件名倒序（文件名内含时间戳）
    std::sort(photos_.begin(), photos_.end(),
              [](const PhotoEntry& left, const PhotoEntry& right) {
                  if (left.capturedAt != right.capturedAt) {
                      return left.capturedAt > right.capturedAt;
                  }
                  return left.name > right.name;
              });

    VB_INFO("相册已加载 %zu 张照片: %ls", photos_.size(), directory_.c_str());
}

void PhotoLibrary::Release() {
    photos_.clear();
    photos_.shrink_to_fit();
}

const img::Image* PhotoLibrary::Thumbnail(size_t index) {
    if (index >= photos_.size()) {
        return nullptr;
    }
    PhotoEntry& entry = photos_[index];
    if (!entry.thumbnailTried) {
        entry.thumbnailTried = true;
        entry.thumbnailReady =
            entry.thumbnail.LoadThumbnail(entry.path, kThumbnailMaxEdge);
        if (!entry.thumbnailReady) {
            VB_WARN("生成缩略图失败: %ls", entry.path.c_str());
        }
    }
    return entry.thumbnailReady ? &entry.thumbnail : nullptr;
}

bool PhotoLibrary::Remove(size_t index) {
    if (index >= photos_.size()) {
        return false;
    }
    const std::wstring path = photos_[index].path;
    if (!::DeleteFileW(path.c_str())) {
        VB_ERROR("删除照片失败（错误码 %lu）: %ls", ::GetLastError(), path.c_str());
        return false;
    }
    VB_INFO("已删除照片: %ls", path.c_str());
    photos_.erase(photos_.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

bool PhotoLibrary::Export(size_t index, const std::wstring& targetPath) const {
    if (index >= photos_.size() || targetPath.empty()) {
        return false;
    }
    const std::wstring& source = photos_[index].path;
    if (!::CopyFileW(source.c_str(), targetPath.c_str(), FALSE)) {
        VB_ERROR("照片另存失败（错误码 %lu）: %ls → %ls", ::GetLastError(), source.c_str(),
                 targetPath.c_str());
        return false;
    }
    VB_INFO("照片已另存: %ls → %ls", source.c_str(), targetPath.c_str());
    return true;
}

} // namespace album
} // namespace vb
