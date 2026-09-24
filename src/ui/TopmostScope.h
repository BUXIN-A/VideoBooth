#pragma once

#include <windows.h>

namespace vb {
namespace ui {

// 全屏窗口会被系统按“全屏优化”直接扫描输出，其弹出的普通窗口可能被压在画面之下。
// 弹出以主窗口为拥有者的模态对话框期间，把主窗口临时提到最顶层：子窗口作为拥有窗口
// 由系统保证位于主窗口之上，从而一定可见；构造/析构自动置顶与还原。
class TopmostScope {
public:
    explicit TopmostScope(HWND hwnd) : hwnd_(hwnd) {
        if (hwnd_ != nullptr) {
            ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    ~TopmostScope() {
        if (hwnd_ != nullptr) {
            ::SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    TopmostScope(const TopmostScope&) = delete;
    TopmostScope& operator=(const TopmostScope&) = delete;

private:
    HWND hwnd_ = nullptr;
};

} // namespace ui
} // namespace vb
