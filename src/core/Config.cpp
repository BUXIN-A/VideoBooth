#include "core/Config.h"

#include "core/Paths.h"
#include "util/Json.h"
#include "util/Log.h"
#include "util/Strings.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace vb {
namespace core {
namespace {

// 摄像头参数的合法区间（配置读取与界面写入共用同一份边界）
constexpr int kMinFps = 1;
constexpr int kMaxFps = 240;
constexpr int kMinWidth = 160;
constexpr int kMaxWidth = 7680;
constexpr int kMinHeight = 120;
constexpr int kMaxHeight = 4320;

bool ReadAllBytes(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > (16 * 1024 * 1024)) {
        ::CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ::ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    ::CloseHandle(file);
    if (!ok || read != out.size()) {
        out.clear();
        return false;
    }
    // 跳过 UTF-8 BOM
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

bool WriteAllBytes(const std::wstring& path, const std::string& data) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, data.data(), static_cast<DWORD>(data.size()),
                                &written, nullptr);
    ::CloseHandle(file);
    return ok && written == data.size();
}

std::string StringField(const json::Value& object, const char* key,
                        const std::string& fallback, bool& repaired) {
    const json::Value* value = object.Find(key);
    if (value == nullptr) {
        repaired = true;
        return fallback;
    }
    if (!value->IsString()) {
        repaired = true;
        VB_WARN("配置字段 %s 类型非法，已使用默认值", key);
        return fallback;
    }
    return value->AsString(fallback);
}

bool BoolField(const json::Value& object, const char* key, bool fallback, bool& repaired) {
    const json::Value* value = object.Find(key);
    if (value == nullptr) {
        repaired = true;
        return fallback;
    }
    if (!value->IsBool()) {
        repaired = true;
        VB_WARN("配置字段 %s 类型非法，已使用默认值", key);
        return fallback;
    }
    return value->AsBool(fallback);
}

int IntField(const json::Value& object, const char* key, int fallback, int minValue,
             int maxValue, bool& repaired) {
    const json::Value* value = object.Find(key);
    if (value == nullptr) {
        repaired = true;
        return fallback;
    }
    if (!value->IsNumber()) {
        repaired = true;
        VB_WARN("配置字段 %s 类型非法，已使用默认值", key);
        return fallback;
    }
    const int parsed = value->AsInt(fallback);
    if (parsed < minValue || parsed > maxValue) {
        repaired = true;
        VB_WARN("配置字段 %s 超出范围 [%d, %d]，已使用默认值 %d", key, minValue,
                maxValue, fallback);
        return fallback;
    }
    return parsed;
}

const json::Value& SubObject(const json::Value& root, const char* key, bool& repaired) {
    static const json::Value kEmpty;
    const json::Value* value = root.Find(key);
    if (value == nullptr || !value->IsObject()) {
        repaired = true;
        return kEmpty;
    }
    return *value;
}

json::Value ToJson(const AppConfig& config) {
    json::Value root = json::Value::MakeObject();
    root.Set("appVersion", json::Value(kAppVersion));
    root.Set("configVersion", json::Value(kConfigVersion));
    root.Set("toolbarPosition", json::Value(config.toolbarPosition));
    root.Set("tempFolder", json::Value(WideToUtf8(config.tempFolder)));
    root.Set("saveLog", json::Value(config.saveLog));

    json::Value camera = json::Value::MakeObject();
    camera.Set("defaultCamera", json::Value(WideToUtf8(config.camera.defaultCamera)));
    camera.Set("fps", json::Value(config.camera.fps));
    camera.Set("width", json::Value(config.camera.width));
    camera.Set("height", json::Value(config.camera.height));
    camera.Set("autoExposure", json::Value(config.camera.autoExposure));
    root.Set("camera", std::move(camera));

    json::Value render = json::Value::MakeObject();
    render.Set("vsync", json::Value(config.render.vsync));
    render.Set("doubleBuffer", json::Value(config.render.doubleBuffer));
    render.Set("antialias", json::Value(config.render.antialias));
    root.Set("render", std::move(render));
    return root;
}

} // namespace

bool ConfigStore::PeekSaveLog(const std::wstring& configPath, bool fallback) {
    std::string text;
    if (!ReadAllBytes(configPath, text)) {
        return fallback;
    }
    json::Value root;
    std::string error;
    if (!json::Value::Parse(text, root, &error) || !root.IsObject()) {
        return fallback;
    }
    const json::Value* value = root.Find("saveLog");
    if (value == nullptr || !value->IsBool()) {
        return fallback;
    }
    return value->AsBool(fallback);
}

// 解析可用的临时照片目录：目录不可用时回退到系统默认目录并清空配置项。
// 返回 true 表示发生了回退（配置需要修复后落盘）
bool ConfigStore::ResolvePhotoDir(std::wstring& photoDir) {
    photoDir = config_.tempFolder.empty() ? paths::DefaultPhotoDir() : config_.tempFolder;
    if (paths::EnsureDirectory(photoDir)) {
        return false;
    }
    VB_WARN("临时照片目录不可用（%ls），回退到默认目录", photoDir.c_str());
    photoDir = paths::DefaultPhotoDir();
    paths::EnsureDirectory(photoDir);
    config_.tempFolder.clear();
    return true;
}

bool ConfigStore::Load() {
    const std::wstring configPath = paths::ConfigPath();
    const std::wstring backupPath = paths::ConfigBackupPath();

    json::Value root;
    std::string text;
    bool loaded = false;

    if (paths::FileExists(configPath)) {
        std::string error;
        if (ReadAllBytes(configPath, text) && json::Value::Parse(text, root, &error) &&
            root.IsObject()) {
            loaded = true;
        } else {
            VB_WARN("主配置不可用（%s），尝试从备份恢复", error.c_str());
        }
    } else {
        VB_INFO("未找到配置文件，将生成默认配置: %ls", configPath.c_str());
    }

    if (!loaded && paths::FileExists(backupPath)) {
        std::string error;
        if (ReadAllBytes(backupPath, text) && json::Value::Parse(text, root, &error) &&
            root.IsObject()) {
            loaded = true;
            VB_WARN("已从备份配置恢复: %ls", backupPath.c_str());
        } else {
            VB_WARN("备份配置也不可用（%s）", error.c_str());
        }
    }

    bool repaired = !loaded;
    if (!loaded) {
        root = json::Value::MakeObject();
    }

    AppConfig config;
    config.appVersion = StringField(root, "appVersion", kAppVersion, repaired);
    if (config.appVersion != kAppVersion) {
        VB_INFO("程序版本号更新: %s -> %s", config.appVersion.c_str(), kAppVersion);
        config.appVersion = kAppVersion;
        repaired = true;
    }

    const int configVersion = IntField(root, "configVersion", kConfigVersion, 1,
                                       kConfigVersion, repaired);
    config.configVersion = configVersion;

    const std::string position = StringField(root, "toolbarPosition", "bottom", repaired);
    if (position == "bottom" || position == "sides") {
        config.toolbarPosition = position;
    } else {
        VB_WARN("功能栏位置取值非法（%s），已回退为 bottom", position.c_str());
        config.toolbarPosition = "bottom";
        repaired = true;
    }
    config.tempFolder = Utf8ToWide(StringField(root, "tempFolder", "", repaired));
    config.saveLog = BoolField(root, "saveLog", false, repaired);

    const json::Value& camera = SubObject(root, "camera", repaired);
    config.camera.defaultCamera =
        Utf8ToWide(StringField(camera, "defaultCamera", "", repaired));
    config.camera.fps = IntField(camera, "fps", 30, kMinFps, kMaxFps, repaired);
    config.camera.width = IntField(camera, "width", 1920, kMinWidth, kMaxWidth, repaired);
    config.camera.height = IntField(camera, "height", 1080, kMinHeight, kMaxHeight, repaired);
    config.camera.autoExposure = BoolField(camera, "autoExposure", false, repaired);

    const json::Value& render = SubObject(root, "render", repaired);
    config.render.vsync = BoolField(render, "vsync", false, repaired);
    config.render.doubleBuffer = BoolField(render, "doubleBuffer", true, repaired);
    config.render.antialias = BoolField(render, "antialias", false, repaired);

    config_ = config;

    // 解析临时照片目录：配置不可用时回退到默认目录
    std::wstring photoDir;
    if (ResolvePhotoDir(photoDir)) {
        repaired = true;
    }
    photoDir_ = photoDir;

    if (repaired) {
        VB_INFO("配置已合并默认值并写回磁盘");
        Save();
    }
    VB_INFO("配置加载完成: 摄像头=%ls, %dx%d@%dfps, 功能栏=%s, 临时目录=%ls",
            config_.camera.defaultCamera.empty() ? L"(未指定)" : config_.camera.defaultCamera.c_str(),
            config_.camera.width, config_.camera.height, config_.camera.fps,
            config_.toolbarPosition.c_str(), photoDir_.c_str());
    return true;
}

bool ConfigStore::Save() {
    const std::string text = ToJson(config_).Dump(2) + "\n";
    const std::wstring configPath = paths::ConfigPath();
    if (!WriteAllBytes(configPath, text)) {
        VB_ERROR("写入配置失败: %ls", configPath.c_str());
        return false;
    }
    const std::wstring backupPath = paths::ConfigBackupPath();
    if (!::CopyFileW(configPath.c_str(), backupPath.c_str(), FALSE)) {
        VB_WARN("更新配置备份失败: %ls", backupPath.c_str());
    }
    return true;
}

bool ConfigStore::SetDefaultCamera(const std::wstring& deviceId) {
    if (EqualsIgnoreCase(config_.camera.defaultCamera, deviceId)) {
        return true;
    }
    config_.camera.defaultCamera = deviceId;
    return Save();
}

bool ConfigStore::Apply(const AppConfig& config) {
    config_ = config;
    // 固定版本号并校正取值范围，避免界面传入非法值
    config_.appVersion = kAppVersion;
    config_.configVersion = kConfigVersion;
    if (config_.toolbarPosition != "bottom" && config_.toolbarPosition != "sides") {
        config_.toolbarPosition = "bottom";
    }
    config_.camera.fps = std::max(kMinFps, std::min(kMaxFps, config_.camera.fps));
    config_.camera.width = std::max(kMinWidth, std::min(kMaxWidth, config_.camera.width));
    config_.camera.height = std::max(kMinHeight, std::min(kMaxHeight, config_.camera.height));

    std::wstring photoDir;
    ResolvePhotoDir(photoDir);
    photoDir_ = photoDir;

    VB_INFO("设置已写入: 摄像头=%ls, %dx%d@%dfps, 自动曝光=%d, 功能栏=%s, 垂直同步=%d, "
            "双缓冲=%d, 抗锯齿=%d, 保存日志=%d, 临时目录=%ls",
            config_.camera.defaultCamera.empty() ? L"(自动)" : config_.camera.defaultCamera.c_str(),
            config_.camera.width, config_.camera.height, config_.camera.fps,
            config_.camera.autoExposure ? 1 : 0, config_.toolbarPosition.c_str(),
            config_.render.vsync ? 1 : 0, config_.render.doubleBuffer ? 1 : 0,
            config_.render.antialias ? 1 : 0, config_.saveLog ? 1 : 0, photoDir_.c_str());
    return Save();
}

} // namespace core
} // namespace vb
