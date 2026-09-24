#include "ui/Resources.h"

#include "core/Paths.h"
#include "util/Log.h"

namespace vb {
namespace ui {

bool Resources::Init(const std::wstring& assetsDir) {
    assetsDir_ = assetsDir;
    cache_.clear();
    if (!paths::EnsureDirectory(assetsDir_)) {
        VB_ERROR("资源目录不可用: %ls", assetsDir_.c_str());
        return false;
    }
    return true;
}

const img::Image* Resources::Get(const wchar_t* fileName) {
    if (fileName == nullptr || assetsDir_.empty()) {
        return nullptr;
    }
    const std::wstring name(fileName);
    const auto existing = cache_.find(name);
    if (existing != cache_.end()) {
        return existing->second.get();
    }

    auto image = std::make_unique<img::Image>();
    if (!image->LoadFromFile(paths::JoinPath(assetsDir_, name))) {
        // 加载失败时缓存空对象，避免每帧重复尝试
        VB_WARN("图标资源缺失: %ls", name.c_str());
        cache_.emplace(name, nullptr);
        return nullptr;
    }
    const img::Image* result = image.get();
    cache_.emplace(name, std::move(image));
    return result;
}

} // namespace ui
} // namespace vb
