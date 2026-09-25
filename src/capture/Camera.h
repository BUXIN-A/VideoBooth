#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct IMFSourceReader;
struct IMFMediaSource;

namespace vb {
namespace capture {

struct CameraInfo {
    std::wstring id;    // 设备符号链接（配置持久化使用）
    std::wstring name;  // 友好名称（界面展示使用）
};

// 枚举系统当前可用摄像头
std::vector<CameraInfo> EnumerateCameras();

// 规范化后的视频帧：自上而下、32 位 BGRA（alpha 固定 255）
struct Frame {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    int stride = 0;
    uint64_t index = 0;
    int64_t timestampMs = 0;
};

using FramePtr = std::shared_ptr<const Frame>;

// Media Foundation SourceReader 采集，独立采集线程；渲染线程通过 TakeLatest 取帧
class CameraCapture {
public:
    CameraCapture() = default;
    ~CameraCapture();

    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    bool Open(const std::wstring& deviceId, int width, int height, int fps,
              bool autoExposure);
    void Close();

    bool IsOpen() const { return open_.load(); }

    // 最小化等场景下暂停拉流（保留设备句柄，恢复迅速）
    void SetActive(bool active) { active_.store(active); }
    bool IsActive() const { return active_.load(); }

    // 取走最新帧，无新帧时返回 nullptr
    FramePtr TakeLatest();

    const std::wstring& deviceId() const { return deviceId_; }
    const std::wstring& deviceName() const { return deviceName_; }

    // 采集线程是否因设备驱动无响应而被分离（此时不应再释放 MF 环境）
    bool IsThreadLeaked() const { return threadBlocked_; }

private:
    enum class OpenResult { Pending, Ok, Failed };

    struct OpenRequest {
        std::wstring deviceId;
        int width = 1280;
        int height = 720;
        int fps = 30;
        bool autoExposure = false;
    };

    void ThreadMain();
    bool OpenOnCaptureThread(const OpenRequest& request);
    void CaptureLoop();
    void Publish(const uint8_t* scan0, ptrdiff_t pitch, int width, int height);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> active_{true};
    std::atomic<bool> open_{false};
    std::atomic<bool> threadFinished_{false};
    bool threadBlocked_ = false;

    std::mutex openMutex_;
    OpenResult openResult_ = OpenResult::Pending;
    OpenRequest request_;

    std::mutex frameMutex_;
    FramePtr latest_;

    // 仅采集线程访问
    IMFSourceReader* reader_ = nullptr;
    // 协商后的采集分辨率，采集循环按此尺寸打包帧（避免每帧查询媒体类型）
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    // 受 sourceMutex_ 保护：关闭流程可能从界面线程调用 Shutdown 以取消阻塞的读取
    std::mutex sourceMutex_;
    IMFMediaSource* source_ = nullptr;
    uint64_t frameIndex_ = 0;

    std::wstring deviceId_;
    std::wstring deviceName_;
    std::atomic<int> fps_{30};
};

} // namespace capture
} // namespace vb
