#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vb {
namespace img {

// 后台 JPG 保存线程：界面线程提交像素，工作线程串行执行 WIC 编码。
// 整帧 JPG 编码在低配设备上可达上百毫秒，放到后台避免阻塞界面与渲染。
class JpegSaveQueue {
public:
    JpegSaveQueue() = default;
    ~JpegSaveQueue();

    JpegSaveQueue(const JpegSaveQueue&) = delete;
    JpegSaveQueue& operator=(const JpegSaveQueue&) = delete;

    // 启动工作线程（重复调用无副作用）
    void Start();
    // 停止工作线程：退出前会把已排队任务全部保存完成
    void Stop();

    // 提交一次 JPG 保存，pixels 所有权转移给队列；工作线程未运行时同步保存
    void Submit(const std::wstring& path, std::vector<uint8_t> pixels, int width, int height,
                int stride, int quality);

    // 累计完成任务数（成功与失败均计入），界面据此在完成后刷新相册
    unsigned long long completedCount() const { return completed_.load(); }
    // 排队中的任务数
    size_t pendingCount() const;

private:
    struct Task {
        std::wstring path;
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        int stride = 0;
        int quality = 85;
    };

    void ThreadMain();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Task> tasks_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<unsigned long long> completed_{0};
};

} // namespace img
} // namespace vb
