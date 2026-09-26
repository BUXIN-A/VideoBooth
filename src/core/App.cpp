#include "core/App.h"

#include "core/Paths.h"
#include "ui/CameraSelectDialog.h"
#include "util/Log.h"
#include "util/Strings.h"

#include <mfapi.h>
#include <timeapi.h>

namespace vb {
namespace core {
namespace {

constexpr wchar_t kSingleInstanceMutex[] = L"Global\\VideoBooth.SingleInstance.v1";
constexpr int kSplashMinimumDurationMs = 2000;

} // namespace

App::~App() {
    ShutdownComAndMedia();
    LogShutdown();
}

int App::Run(HINSTANCE instance) {
    instance_ = instance;

    // 提高定时器精度，保证渲染节拍稳定
    ::timeBeginPeriod(1);

    // 日志开关需在配置完整加载前生效：先预读配置，关闭时不创建日志文件
    LogInit(paths::LogPath(), ConfigStore::PeekSaveLog(paths::ConfigPath(), false));
    VB_INFO("=== VideoBooth 启动，程序版本 %s ===", kAppVersion);

    if (!InitComAndMedia()) {
        ::MessageBoxW(nullptr, L"初始化多媒体组件失败，程序无法启动。", L"VideoBooth",
                      MB_ICONERROR | MB_OK);
        return 1;
    }

    // 单实例检测：已有实例时激活其窗口后退出
    if (!singleInstance_.Acquire(kSingleInstanceMutex, ui::kMainWindowClassName)) {
        VB_INFO("程序已在运行，本次启动退出");
        return 0;
    }

    splash_.Create(instance_, paths::AssetPath(L"logo.png"), kSplashMinimumDurationMs);
    PumpMessages();

    LoadConfig();
    // 配置完整加载后校准日志开关（与启动预读结果一致）
    LogSetFileEnabled(config_.Get().saveLog);
    LoadAssets();

    DetectAndOpenCamera();

    while (!splash_.MinimumElapsed()) {
        PumpMessages();
        ::Sleep(10);
    }
    splash_.Close();

    if (!CreateMainWindow()) {
        VB_ERROR("创建主窗口失败，程序退出");
        CleanupPhotoDirectory();
        return 1;
    }

    window_->RunMessageLoop();
    VB_INFO("消息循环结束，开始退出清理");

    window_->Destroy();
    window_.reset();
    camera_.Close();
    CleanupPhotoDirectory();

    ::timeEndPeriod(1);
    VB_INFO("=== VideoBooth 已退出 ===");
    return 0;
}

bool App::InitComAndMedia() {
    const HRESULT comHr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE) {
        VB_ERROR("COM 初始化失败: %ls", HresultToWide(comHr).c_str());
        return false;
    }
    comInitialized_ = SUCCEEDED(comHr);

    const HRESULT mfHr = ::MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(mfHr)) {
        VB_ERROR("Media Foundation 初始化失败: %ls", HresultToWide(mfHr).c_str());
        return false;
    }
    mediaStarted_ = true;
    return true;
}

void App::ShutdownComAndMedia() {
    if (mediaStarted_) {
        if (camera_.IsThreadLeaked()) {
            // 采集线程仍可能在使用 MF 内部对象，此处不释放 MF 环境，
            // 交由进程退出时统一回收，避免析构期间崩溃
            VB_WARN("采集线程被分离，跳过 Media Foundation 释放");
        } else {
            ::MFShutdown();
        }
        mediaStarted_ = false;
    }
    if (comInitialized_) {
        ::CoUninitialize();
        comInitialized_ = false;
    }
}

void App::LoadConfig() {
    config_.Load();
    VB_INFO("临时照片目录: %ls", config_.PhotoDir().c_str());
}

void App::LoadAssets() {
    if (!resources_.Init(paths::AssetsDir())) {
        VB_ERROR("资源目录初始化失败");
    }

    errorImage_ = std::make_unique<img::Image>();
    if (!errorImage_->LoadFromFile(paths::AssetPath(L"error.png"))) {
        VB_WARN("占位图 error.png 加载失败，无摄像头时将显示空画面");
        errorImage_.reset();
    }
}

bool App::DetectAndOpenCamera() {
    const std::vector<capture::CameraInfo> devices = capture::EnumerateCameras();
    if (devices.empty()) {
        // 没有任何摄像头：继续启动，画面显示 error.png
        VB_WARN("未检测到任何摄像头设备，将以占位画面启动");
        return false;
    }

    // 1. 配置文件中的默认摄像头仍然可用时直接使用
    const CameraConfig& cameraConfig = config_.Get().camera;
    if (!cameraConfig.defaultCamera.empty()) {
        for (const auto& device : devices) {
            if (!EqualsIgnoreCase(device.id, cameraConfig.defaultCamera)) {
                continue;
            }
            if (OpenCamera(cameraConfig.defaultCamera)) {
                VB_INFO("使用配置中的默认摄像头: %ls", camera_.deviceName().c_str());
                return true;
            }
            VB_WARN("配置中的默认摄像头无法打开: %ls", device.name.c_str());
            break;
        }
        if (!camera_.IsOpen()) {
            VB_WARN("配置中的默认摄像头已不可用");
        }
    }

    // 2. 仅有一个设备时无需选择，直接采用并写入配置
    if (devices.size() == 1) {
        if (OpenCamera(devices.front().id)) {
            config_.SetDefaultCamera(camera_.deviceId());
            VB_INFO("仅有 1 个摄像头，已作为默认摄像头写入配置: %ls",
                    camera_.deviceName().c_str());
            return true;
        }
        return false;
    }

    // 3. 让用户从设备列表中选择默认摄像头（并更新配置文件）
    splash_.Close(); // 先收起启动画面再弹选择框
    const int selected = ui::CameraSelectDialog::Run(instance_, devices);
    if (selected >= 0 && selected < static_cast<int>(devices.size())) {
        if (OpenCamera(devices[selected].id)) {
            config_.SetDefaultCamera(camera_.deviceId());
            VB_INFO("用户选择的默认摄像头已写入配置: %ls", camera_.deviceName().c_str());
            return true;
        }
        VB_WARN("用户选择的摄像头无法打开，尝试其他设备");
    } else {
        VB_INFO("用户未选择摄像头，临时使用可用设备");
    }

    // 4. 用户取消或所选设备打开失败：临时使用可用设备（不写入配置）
    for (const auto& device : devices) {
        if (OpenCamera(device.id)) {
            VB_INFO("临时使用摄像头（未写入配置）: %ls", camera_.deviceName().c_str());
            return true;
        }
    }
    VB_ERROR("所有摄像头均无法打开");
    return false;
}

bool App::OpenCamera(const std::wstring& deviceId) {
    const CameraConfig& cameraConfig = config_.Get().camera;
    return camera_.Open(deviceId, cameraConfig.width, cameraConfig.height, cameraConfig.fps,
                        cameraConfig.autoExposure);
}

void App::MaintainCamera() {
    if (camera_.IsOpen()) {
        return;
    }

    // 热插拔：依次尝试上次使用的设备、配置中的默认摄像头、当前可用设备
    const std::wstring lastDevice = camera_.deviceId();
    if (!lastDevice.empty() && OpenCamera(lastDevice)) {
        VB_INFO("摄像头已重新连接: %ls", camera_.deviceName().c_str());
        return;
    }

    const std::wstring configured = config_.Get().camera.defaultCamera;
    if (!configured.empty() && !EqualsIgnoreCase(configured, lastDevice) &&
        OpenCamera(configured)) {
        VB_INFO("默认摄像头已重新连接: %ls", camera_.deviceName().c_str());
        return;
    }

    const std::vector<capture::CameraInfo> devices = capture::EnumerateCameras();
    for (const auto& device : devices) {
        if (OpenCamera(device.id)) {
            // 自动切换不修改配置，用户的默认摄像头选择保持有效
            VB_INFO("已自动切换到可用摄像头: %ls（未写入配置）", camera_.deviceName().c_str());
            return;
        }
    }
    VB_WARN("当前无可用摄像头，等待设备接入");
}

bool App::CreateMainWindow() {
    ui::MainWindowDeps deps;
    deps.resources = &resources_;
    deps.camera = &camera_;
    deps.errorImage = errorImage_.get();
    deps.configStore = &config_;
    deps.onExitRequested = [this]() {
        if (exitRequested_) {
            return;
        }
        exitRequested_ = true;
        if (window_) {
            window_->RequestClose();
        }
    };
    deps.onSettingsApplied = [this](const AppConfig& updated) { ApplySettings(updated); };
    deps.onCameraMaintain = [this]() { MaintainCamera(); };

    window_ = std::make_unique<ui::MainWindow>();
    if (!window_->Create(instance_, deps)) {
        window_.reset();
        return false;
    }
    return true;
}

void App::ApplySettings(const AppConfig& updated) {
    const AppConfig before = config_.Get();
    if (!config_.Apply(updated)) {
        VB_ERROR("设置保存失败，请检查程序目录写权限");
    }

    // 日志开关立即生效（先记录再关闭，保证切换动作本身可见）
    if (before.saveLog != config_.Get().saveLog) {
        VB_INFO("日志文件写入已%s", config_.Get().saveLog ? "开启" : "关闭");
        LogSetFileEnabled(config_.Get().saveLog);
    }

    if (before.render.doubleBuffer != config_.Get().render.doubleBuffer ||
        before.render.antialias != config_.Get().render.antialias) {
        VB_WARN("双缓冲 / 抗锯齿设置已保存，将在下次启动程序时生效");
    }

    const CameraConfig& previous = before.camera;
    const CameraConfig& current = config_.Get().camera;
    const bool cameraChanged =
        !EqualsIgnoreCase(previous.defaultCamera, current.defaultCamera) ||
        previous.fps != current.fps || previous.width != current.width ||
        previous.height != current.height || previous.autoExposure != current.autoExposure;
    if (cameraChanged) {
        ReopenCamera();
    }
}

void App::ReopenCamera() {
    VB_INFO("摄像头设置已修改，重新打开采集设备");
    camera_.Close();

    const CameraConfig& cameraConfig = config_.Get().camera;
    if (!cameraConfig.defaultCamera.empty() &&
        OpenCamera(cameraConfig.defaultCamera)) {
        VB_INFO("已按新设置打开摄像头: %ls", camera_.deviceName().c_str());
        return;
    }

    for (const auto& device : capture::EnumerateCameras()) {
        if (OpenCamera(device.id)) {
            config_.SetDefaultCamera(camera_.deviceId());
            VB_INFO("已切换到可用摄像头: %ls", camera_.deviceName().c_str());
            return;
        }
    }
    VB_WARN("按新设置未能打开任何摄像头，画面将显示占位图");
}

void App::PumpMessages() {
    MSG message = {};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

void App::CleanupPhotoDirectory() {
    const std::wstring directory = config_.PhotoDir();
    if (directory.empty()) {
        return;
    }
    // 临时照片与每张照片对应的笔迹文件一并清理
    const wchar_t* const patterns[] = {L"IMG_*.jpg", L"*.ann.png"};
    int removed = 0;
    for (const wchar_t* patternText : patterns) {
        const std::wstring pattern = paths::JoinPath(directory, patternText);
        WIN32_FIND_DATAW data = {};
        HANDLE find = ::FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                continue;
            }
            const std::wstring file = paths::JoinPath(directory, data.cFileName);
            if (::DeleteFileW(file.c_str())) {
                ++removed;
            }
        } while (::FindNextFileW(find, &data));
        ::FindClose(find);
    }
    VB_INFO("退出清理：已删除 %d 个临时文件（照片与笔迹）", removed);
}

} // namespace core
} // namespace vb
