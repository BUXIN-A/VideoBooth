#pragma once

#include <string>
#include <unordered_map>
#include <memory>

#include "util/Image.h"

namespace vb {
namespace ui {

// 图标资源缓存：按文件名懒加载 assets 目录下的 PNG
class Resources {
public:
    bool Init(const std::wstring& assetsDir);

    const img::Image* Get(const wchar_t* fileName);
    const std::wstring& assetsDir() const { return assetsDir_; }

private:
    std::wstring assetsDir_;
    std::unordered_map<std::wstring, std::unique_ptr<img::Image>> cache_;
};

} // namespace ui
} // namespace vb
