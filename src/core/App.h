#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "capture/Camera.h"
#include "core/Config.h"
#include "core/SingleInstance.h"
#include "ui/MainWindow.h"
#include "ui/Resources.h"
#include "ui/SplashWindow.h"

namespace vb {
namespace core {

// 应用生命周期：启动流程、摄像头检测、退出清理
class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run(HINSTANCE instance);

private:
    bool InitComAndMedia();
    void ShutdownComAndMedia();
    void LoadConfig();
    void LoadAssets();
    bool DetectAndOpenCamera();
    bool OpenCamera(const std::wstring& deviceId);
    void MaintainCamera();
    // 设置保存后应用：落盘并在摄像头参数变化时重新打开设备
    void ApplySettings(const AppConfig& updated);
    void ReopenCamera();
    bool CreateMainWindow();
    void PumpMessages();
    void CleanupPhotoDirectory();

    HINSTANCE instance_ = nullptr;
    SingleInstance singleInstance_;
    ConfigStore config_;
    ui::Resources resources_;
    ui::SplashWindow splash_;
    // camera_ 声明早于 window_，保证窗口先于采集对象析构
    capture::CameraCapture camera_;
    std::unique_ptr<ui::MainWindow> window_;
    std::unique_ptr<img::Image> errorImage_;
    bool comInitialized_ = false;
    bool mediaStarted_ = false;
    bool exitRequested_ = false;
};

} // namespace core
} // namespace vb
