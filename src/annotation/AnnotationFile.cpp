#include "annotation/AnnotationFile.h"

#include "core/Paths.h"
#include "util/Image.h"
#include "util/Log.h"

namespace vb {
namespace annotation {
namespace {

constexpr wchar_t kAnnotationSuffix[] = L".ann.png";

} // namespace

std::wstring AnnotationPathFor(const std::wstring& directory, const std::wstring& pictureKey) {
    if (directory.empty()) {
        return std::wstring();
    }
    const std::wstring stem = pictureKey.empty() ? std::wstring(kLiveAnnotationKey) : pictureKey;
    return paths::JoinPath(directory, stem + kAnnotationSuffix);
}

bool SaveToFile(const std::wstring& path, const StrokeLayer& layer) {
    if (path.empty()) {
        return false;
    }
    if (!layer.valid() || layer.empty()) {
        RemoveFile(path);
        return true;
    }
    if (!img::SavePng(path, layer.pixels(), layer.width(), layer.height(), layer.stride())) {
        VB_WARN("保存笔迹文件失败: %ls", path.c_str());
        return false;
    }
    VB_INFO("笔迹已保存: %ls (%dx%d)", path.c_str(), layer.width(), layer.height());
    return true;
}

bool LoadFromFile(const std::wstring& path, StrokeLayer& layer, int expectedWidth,
                  int expectedHeight) {
    if (path.empty() || !paths::FileExists(path)) {
        return false;
    }
    img::Image image;
    if (!image.LoadFromFile(path)) {
        VB_WARN("笔迹文件解码失败: %ls", path.c_str());
        return false;
    }
    if (image.width() != expectedWidth || image.height() != expectedHeight) {
        VB_WARN("笔迹尺寸与画面不符，已忽略: %ls（%dx%d ≠ %dx%d）", path.c_str(), image.width(),
                image.height(), expectedWidth, expectedHeight);
        return false;
    }
    if (!layer.ImportPixels(image.pixels(), image.width(), image.height(), image.stride())) {
        return false;
    }
    VB_INFO("笔迹已载入: %ls (%dx%d)", path.c_str(), image.width(), image.height());
    return true;
}

bool RemoveFile(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    if (::DeleteFileW(path.c_str())) {
        VB_INFO("已删除笔迹文件: %ls", path.c_str());
        return true;
    }
    return ::GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool CompositeFileInto(const std::wstring& path, uint8_t* target, int width, int height,
                       int stride) {
    if (path.empty() || target == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    img::Image image;
    if (!image.LoadFromFile(path)) {
        return false;
    }
    if (image.width() != width || image.height() != height) {
        VB_WARN("笔迹尺寸与照片不符，跳过合成: %ls", path.c_str());
        return false;
    }
    StrokeLayer layer;
    if (!layer.ImportPixels(image.pixels(), image.width(), image.height(), image.stride())) {
        return false;
    }
    layer.Composite(target, stride);
    return true;
}

} // namespace annotation
} // namespace vb
