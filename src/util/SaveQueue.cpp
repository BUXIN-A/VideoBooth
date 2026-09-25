#include "util/SaveQueue.h"

#include <windows.h>

#include <objbase.h>

#include "util/Image.h"
#include "util/Log.h"

namespace vb {
namespace img {

JpegSaveQueue::~JpegSaveQueue() {
    Stop();
}

void JpegSaveQueue::Start() {
    if (running_.exchange(true)) {
        return; // 已在运行
    }
    thread_ = std::thread(&JpegSaveQueue::ThreadMain, this);
}

void JpegSaveQueue::Stop() {
    running_.store(false);
    condition_.notify_all();
    if (thread_.joinable()) {
        // 工作线程退出前会先把队列中剩余任务保存完
        thread_.join();
    }
}

size_t JpegSaveQueue::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void JpegSaveQueue::Submit(const std::wstring& path, std::vector<uint8_t> pixels, int width,
                           int height, int stride, int quality) {
    Task task;
    task.path = path;
    task.pixels = std::move(pixels);
    task.width = width;
    task.height = height;
    task.stride = stride;
    task.quality = quality;

    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_.load()) {
            tasks_.push_back(std::move(task));
            queued = true;
        }
    }
    if (queued) {
        condition_.notify_one();
        return;
    }

    // 工作线程未运行：退化为同步保存，避免照片丢失
    if (SaveJpeg(task.path, task.pixels.data(), task.width, task.height, task.stride,
                 task.quality)) {
        VB_INFO("照片已保存: %ls (%dx%d)", task.path.c_str(), task.width, task.height);
    } else {
        VB_ERROR("照片保存失败: %ls", task.path.c_str());
    }
    completed_.fetch_add(1);
}

void JpegSaveQueue::ThreadMain() {
    // WIC 编码要求本线程初始化 COM（与界面线程各自独立，互不影响）
    const HRESULT comHr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return !running_.load() || !tasks_.empty(); });
            if (tasks_.empty()) {
                if (!running_.load()) {
                    break;
                }
                continue;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        if (SaveJpeg(task.path, task.pixels.data(), task.width, task.height, task.stride,
                     task.quality)) {
            VB_INFO("照片已保存: %ls (%dx%d)", task.path.c_str(), task.width, task.height);
        } else {
            VB_ERROR("照片保存失败: %ls", task.path.c_str());
        }
        completed_.fetch_add(1);
    }

    if (SUCCEEDED(comHr)) {
        ::CoUninitialize();
    }
}

} // namespace img
} // namespace vb
