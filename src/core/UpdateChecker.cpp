#include "core/UpdateChecker.h"

#include "core/Config.h"
#include "util/Json.h"
#include "util/Log.h"
#include "util/Strings.h"

#include <windows.h>
#include <winhttp.h>

namespace vb {
namespace core {
namespace {

// 自动代理模式在 Windows 8.1 之前不可用，缺失时补充定义（目标系统为 Windows 10）
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/BUXIN-A/VideoBooth/releases/latest";
constexpr wchar_t kReleasePage[] = L"https://github.com/BUXIN-A/VideoBooth/releases/latest";
constexpr int kTimeoutMs = 3000;
constexpr size_t kMaxResponseBytes = 256 * 1024;

// WinHTTP 句柄的 RAII 包装
class Handle {
public:
    explicit Handle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~Handle() {
        if (handle_ != nullptr) {
            ::WinHttpCloseHandle(handle_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

std::wstring UserAgent() {
    return std::wstring(L"VideoBooth/") + Utf8ToWide(kAppVersion);
}

// 请求最新 release 接口并读取响应体（仅 HTTP 200 视为成功）
bool FetchLatestRelease(std::string& body) {
    body.clear();
    const Handle session(::WinHttpOpen(UserAgent().c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        VB_WARN("检查更新失败：无法初始化 WinHTTP");
        return false;
    }
    ::WinHttpSetTimeouts(session.get(), kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs * 2);

    const Handle connect(::WinHttpConnect(session.get(), kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    const Handle request(::WinHttpOpenRequest(connect.get(), L"GET", kApiPath, nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    if (!connect || !request) {
        return false;
    }
    // GitHub API 要求携带 User-Agent（已在会话上设置），并推荐声明接受的媒体类型
    // 长度取 -1 表示请求头以字符串结尾
    ::WinHttpAddRequestHeaders(request.get(), L"Accept: application/vnd.github+json",
                               static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    if (!::WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !::WinHttpReceiveResponse(request.get(), nullptr)) {
        VB_WARN("检查更新失败：请求未完成（错误码 %lu）", ::GetLastError());
        return false;
    }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    if (!::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size,
                               WINHTTP_NO_HEADER_INDEX)) {
        return false;
    }
    if (statusCode != 200) {
        VB_WARN("检查更新失败：服务返回状态码 %lu", statusCode);
        return false;
    }

    for (;;) {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request.get(), &available) || available == 0) {
            break;
        }
        if (body.size() + available > kMaxResponseBytes) {
            VB_WARN("检查更新失败：响应内容过大");
            return false;
        }
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!::WinHttpReadData(request.get(), body.data() + offset, available, &read) ||
            read == 0) {
            body.resize(offset);
            return false;
        }
        body.resize(offset + read);
    }
    return !body.empty();
}

// 解析形如 "v1.2.3" 的版本号，缺失段按 0 处理；无法解析返回 false
bool ParseVersion(const std::wstring& text, int parts[3]) {
    parts[0] = 0;
    parts[1] = 0;
    parts[2] = 0;
    size_t i = 0;
    if (i < text.size() && (text[i] == L'v' || text[i] == L'V')) {
        ++i;
    }
    int segment = 0;
    bool sawDigit = false;
    while (i < text.size() && segment < 3) {
        const wchar_t character = text[i];
        if (character >= L'0' && character <= L'9') {
            parts[segment] = parts[segment] * 10 + static_cast<int>(character - L'0');
            sawDigit = true;
            ++i;
            continue;
        }
        if (character == L'.' && sawDigit) {
            ++segment;
            sawDigit = false;
            ++i;
            continue;
        }
        break; // 遇到 "-beta" 等后缀即结束解析
    }
    return sawDigit || segment > 0;
}

} // namespace

int CompareVersions(const std::wstring& a, const std::wstring& b) {
    int left[3] = {};
    int right[3] = {};
    if (!ParseVersion(a, left) || !ParseVersion(b, right)) {
        return 2;
    }
    for (int i = 0; i < 3; ++i) {
        if (left[i] != right[i]) {
            return left[i] > right[i] ? 1 : -1;
        }
    }
    return 0;
}

UpdateChecker::~UpdateChecker() {
    Stop();
}

void UpdateChecker::Start(const std::wstring& currentVersion) {
    if (shared_->running.exchange(true)) {
        return; // 已有检查在进行
    }
    shared_->status.store(Status::Checking);
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->latestVersion.clear();
        shared_->releaseUrl.clear();
    }
    if (thread_.joinable()) {
        thread_.detach(); // 上一次检查线程已结束
    }
    const std::shared_ptr<Shared> shared = shared_;
    thread_ = std::thread(&UpdateChecker::ThreadMain, shared, currentVersion);
}

void UpdateChecker::Stop() {
    shared_->running.store(false);
    if (thread_.joinable()) {
        // 线程只持有共享状态块，可安全地自行结束
        thread_.detach();
    }
}

std::wstring UpdateChecker::latestVersion() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->latestVersion;
}

std::wstring UpdateChecker::releaseUrl() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->releaseUrl;
}

void UpdateChecker::ThreadMain(const std::shared_ptr<Shared>& shared, std::wstring currentVersion) {
    std::string body;
    const bool fetched = shared->running.load() && FetchLatestRelease(body);

    Status result = Status::Failed;
    std::wstring version;
    std::wstring url;
    if (fetched) {
        json::Value root;
        std::string error;
        const json::Value* tag = nullptr;
        const json::Value* html = nullptr;
        if (json::Value::Parse(body, root, &error) && root.IsObject()) {
            tag = root.Find("tag_name");
            html = root.Find("html_url");
        }
        if (tag != nullptr && tag->IsString()) {
            version = Utf8ToWide(tag->AsString(""));
            if (!version.empty() && (version[0] == L'v' || version[0] == L'V')) {
                version.erase(0, 1);
            }
            if (html != nullptr && html->IsString()) {
                url = Utf8ToWide(html->AsString(""));
            }
            if (url.empty()) {
                url = kReleasePage;
            }

            const int comparison = CompareVersions(version, currentVersion);
            if (comparison == 2) {
                VB_WARN("检查更新失败：无法解析版本号（最新 %ls，当前 %ls）", version.c_str(),
                        currentVersion.c_str());
            } else if (comparison > 0) {
                result = Status::UpdateAvailable;
                VB_INFO("检查更新：发现新版本 %ls（当前 %ls）", version.c_str(),
                        currentVersion.c_str());
            } else {
                result = Status::UpToDate;
                VB_INFO("检查更新：已是最新版本 %ls", currentVersion.c_str());
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->latestVersion = version;
        shared->releaseUrl = url;
    }
    shared->status.store(result);
    shared->running.store(false);
}

} // namespace core
} // namespace vb
