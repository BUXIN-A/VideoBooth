#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace vb {
namespace core {

// 检查 GitHub 最新 release：网络请求在后台线程执行，主线程通过 status() 轮询结果。
// 线程只持有共享状态块，因此对象销毁时无需等待请求超时。
class UpdateChecker {
public:
    enum class Status { Idle, Checking, UpToDate, UpdateAvailable, Failed };

    UpdateChecker() = default;
    ~UpdateChecker();
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    // 开始检查（已在检查中时忽略）；currentVersion 形如 "1.1.0"
    void Start(const std::wstring& currentVersion);
    void Stop();

    Status status() const { return shared_->status.load(); }
    // 远端最新版本号（形如 "1.1.0"），无结果时为空
    std::wstring latestVersion() const;
    // 对应 release 页面地址
    std::wstring releaseUrl() const;

private:
    struct Shared {
        std::atomic<Status> status{Status::Idle};
        std::atomic<bool> running{false};
        mutable std::mutex mutex;
        std::wstring latestVersion;
        std::wstring releaseUrl;
    };

    static void ThreadMain(const std::shared_ptr<Shared>& shared, std::wstring currentVersion);

    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::thread thread_;
};

// 版本比较：a > b 返回 1，a < b 返回 -1，相等返回 0，无法解析返回 2
int CompareVersions(const std::wstring& a, const std::wstring& b);

} // namespace core
} // namespace vb
