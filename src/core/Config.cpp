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
// GUI 大小倍率的合法区间
constexpr double kMinGuiScale = 0.75;
constexpr double kMaxGuiScale = 2.0;

// 抗锯齿等级仅接受 0 / 2 / 4 / 8
bool IsValidAntialiasLevel(int level) {
    return level == 0 || level == 2 || level == 4 || level == 8;
}

// 锐化等级仅接受 0 / 1 / 2 / 3
bool IsValidSharpenLevel(int level) {
    return level >= 0 && level <= 3;
}

// 采集分辨率：0/0 表示使用摄像头原生最大分辨率，其余须落在合法区间内
bool IsValidResolutionPair(int width, int height) {
    if (width == 0 && height == 0) {
        return true;
    }
    return width >= kMinWidth && width <= kMaxWidth && height >= kMinHeight &&
           height <= kMaxHeight;
}

// 采集分辨率的可读文本（0/0 表示原生最大）
std::string ResolutionText(const CameraConfig& camera) {
    if (camera.IsNativeResolution()) {
        return "原生";
    }
    return FormatA("%dx%d", camera.width, camera.height);
}

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

double NumberField(const json::Value& object, const char* key, double fallback, double minValue,
                   double maxValue, bool& repaired) {
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
    const double parsed = value->AsNumber(fallback);
    if (parsed < minValue || parsed > maxValue) {
        repaired = true;
        VB_WARN("配置字段 %s 超出范围 [%.2f, %.2f]，已使用默认值 %.2f", key, minValue, maxValue,
                fallback);
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
    root.Set("guiScaleAuto", json::Value(config.guiScaleAuto));
    root.Set("guiScale", json::Value(config.guiScale));
    root.Set("rotationQuarter", json::Value(config.rotationQuarter));
    root.Set("fontFamily", json::Value(WideToUtf8(config.fontFamily)));

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
    render.Set("antialiasLevel", json::Value(config.render.antialiasLevel));
    render.Set("sharpenLevel", json::Value(config.render.sharpenLevel));
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
    config.configVersion = kConfigVersion;

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
    config.guiScaleAuto = BoolField(root, "guiScaleAuto", true, repaired);
    config.guiScale = NumberField(root, "guiScale", 1.0, kMinGuiScale, kMaxGuiScale, repaired);
    config.rotationQuarter = IntField(root, "rotationQuarter", 0, 0, 3, repaired);
    config.fontFamily = Utf8ToWide(StringField(root, "fontFamily", "", repaired));

    const json::Value& camera = SubObject(root, "camera", repaired);
    config.camera.defaultCamera =
        Utf8ToWide(StringField(camera, "defaultCamera", "", repaired));
    config.camera.fps = IntField(camera, "fps", 30, kMinFps, kMaxFps, repaired);
    config.camera.width = IntField(camera, "width", 0, 0, kMaxWidth, repaired);
    config.camera.height = IntField(camera, "height", 0, 0, kMaxHeight, repaired);
    // 版本 2 之前默认按固定分辨率采集，升级后重置为原生，避免系统缩放导致画面模糊
    if (configVersion < 2 && !config.camera.IsNativeResolution()) {
        VB_INFO("旧配置采集分辨率 %dx%d 已重置为原生最大分辨率", config.camera.width,
                config.camera.height);
        config.camera.width = 0;
        config.camera.height = 0;
        repaired = true;
    }
    if (!IsValidResolutionPair(config.camera.width, config.camera.height)) {
        VB_WARN("采集分辨率 %dx%d 非法，已回退为原生最大分辨率", config.camera.width,
                config.camera.height);
        config.camera.width = 0;
        config.camera.height = 0;
        repaired = true;
    }
    config.camera.autoExposure = BoolField(camera, "autoExposure", false, repaired);

    const json::Value& render = SubObject(root, "render", repaired);
    config.render.vsync = BoolField(render, "vsync", false, repaired);
    config.render.doubleBuffer = BoolField(render, "doubleBuffer", true, repaired);
    config.render.antialiasLevel = IntField(render, "antialiasLevel", 0, 0, 8, repaired);
    if (!IsValidAntialiasLevel(config.render.antialiasLevel)) {
        config.render.antialiasLevel = 0;
        repaired = true;
    }
    config.render.sharpenLevel = IntField(render, "sharpenLevel", 0, 0, 3, repaired);

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
    VB_INFO("配置加载完成: 摄像头=%ls, %s@%dfps, 功能栏=%s, 临时目录=%ls",
            config_.camera.defaultCamera.empty() ? L"(未指定)" : config_.camera.defaultCamera.c_str(),
            ResolutionText(config_.camera).c_str(), config_.camera.fps,
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
    if (!IsValidResolutionPair(config_.camera.width, config_.camera.height)) {
        config_.camera.width = 0;
        config_.camera.height = 0;
    }
    config_.guiScale = std::max(kMinGuiScale, std::min(kMaxGuiScale, config_.guiScale));
    config_.rotationQuarter = std::max(0, std::min(3, config_.rotationQuarter));
    if (!IsValidAntialiasLevel(config_.render.antialiasLevel)) {
        config_.render.antialiasLevel = 0;
    }
    if (!IsValidSharpenLevel(config_.render.sharpenLevel)) {
        config_.render.sharpenLevel = 0;
    }

    std::wstring photoDir;
    ResolvePhotoDir(photoDir);
    photoDir_ = photoDir;

    VB_INFO("设置已写入: 摄像头=%ls, %s@%dfps, 自动曝光=%d, 功能栏=%s, 垂直同步=%d, "
            "双缓冲=%d, 抗锯齿=%d, 锐化=%d, 字体=%ls, 保存日志=%d, 临时目录=%ls",
            config_.camera.defaultCamera.empty() ? L"(自动)" : config_.camera.defaultCamera.c_str(),
            ResolutionText(config_.camera).c_str(), config_.camera.fps,
            config_.camera.autoExposure ? 1 : 0, config_.toolbarPosition.c_str(),
            config_.render.vsync ? 1 : 0, config_.render.doubleBuffer ? 1 : 0,
            config_.render.antialiasLevel, config_.render.sharpenLevel,
            config_.fontFamily.empty() ? L"(默认)" : config_.fontFamily.c_str(),
            config_.saveLog ? 1 : 0, photoDir_.c_str());
    return Save();
}

} // namespace core
} // namespace vb
