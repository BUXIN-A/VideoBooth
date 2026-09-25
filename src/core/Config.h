#pragma once

#include <string>

namespace vb {
namespace core {

// 程序版本号 / 配置文件版本号
inline constexpr const char* kAppVersion = "0.1.0";
inline constexpr int kConfigVersion = 1;

struct CameraConfig {
    std::wstring defaultCamera;   // 设备符号链接，空表示未指定
    int fps = 30;                 // 采集刷新率
    int width = 1920;             // 采集分辨率
    int height = 1080;
    bool autoExposure = false;    // 默认关闭自动曝光
};

struct RenderConfig {
    bool vsync = false;           // 默认关闭垂直同步
    bool doubleBuffer = true;     // 默认开启双缓冲
    bool antialias = false;       // 多重采样抗锯齿（需重启程序生效）
};

struct AppConfig {
    std::string appVersion = kAppVersion;
    int configVersion = kConfigVersion;
    std::string toolbarPosition = "bottom";   // bottom | sides
    std::wstring tempFolder;                  // 空表示使用默认临时目录
    bool saveLog = false;                     // 是否把运行日志写入日志文件
    CameraConfig camera;
    RenderConfig render;

    bool IsToolbarVertical() const { return toolbarPosition == "sides"; }
};

// 配置读取策略：主配置 -> 备份 -> 字段级校验 + 默认值合并，需要修复时自动落盘
class ConfigStore {
public:
    // 启动早期只取“保存日志”开关（不落盘、不写日志），用于决定是否创建日志文件
    static bool PeekSaveLog(const std::wstring& configPath, bool fallback);

    bool Load();
    bool Save();

    const AppConfig& Get() const { return config_; }

    // 应用界面修改后的配置：校正取值范围、重算临时目录并落盘
    bool Apply(const AppConfig& config);

    // 更新默认摄像头并落盘
    bool SetDefaultCamera(const std::wstring& deviceId);

    // 解析后的临时照片目录（始终可用）
    const std::wstring& PhotoDir() const { return photoDir_; }

private:
    // 解析临时照片目录，必要时回退到默认目录（返回是否需要修复配置）
    bool ResolvePhotoDir(std::wstring& photoDir);

    AppConfig config_;
    std::wstring photoDir_;
};

} // namespace core
} // namespace vb
