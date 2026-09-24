#include "core/SingleInstance.h"

#include "util/Log.h"

namespace vb {
namespace core {

SingleInstance::~SingleInstance() {
    if (mutex_ != nullptr) {
        ::CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstance::Acquire(const std::wstring& mutexName, const wchar_t* windowClass) {
    bool alreadyRunning = false;
    if (mutex_ == nullptr) {
        mutex_ = ::CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (mutex_ == nullptr) {
            VB_WARN("创建单实例互斥体失败，跳过单实例检测");
            return true;
        }
        alreadyRunning = (::GetLastError() == ERROR_ALREADY_EXISTS);
    }

    if (!alreadyRunning) {
        return true;
    }

    // 已有实例：尝试把它的窗口激活到前台
    if (windowClass != nullptr) {
        HWND existing = ::FindWindowW(windowClass, nullptr);
        if (existing != nullptr) {
            if (::IsIconic(existing)) {
                ::ShowWindow(existing, SW_RESTORE);
            }
            ::SetForegroundWindow(existing);
            VB_INFO("检测到已运行的展台程序，已激活其窗口");
            return false;
        }
        // 互斥体存在但窗口未就绪（可能是残留），继续启动
        VB_WARN("检测到残留的单实例互斥体，继续启动当前实例");
    }
    return true;
}

} // namespace core
} // namespace vb
