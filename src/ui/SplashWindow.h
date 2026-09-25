#pragma once

#include <windows.h>

#include <string>

#include "ui/OverlayCanvas.h"

namespace vb {
namespace ui {

// 启动画面：分层窗口居中显示 logo.png
class SplashWindow {
public:
    ~SplashWindow();

    bool Create(HINSTANCE instance, const std::wstring& logoPath, int minimumDurationMs);
    void Close();

    bool MinimumElapsed() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    ULONGLONG startTick_ = 0;
    int minimumDurationMs_ = 1200;
    OverlayCanvas canvas_;
};

} // namespace ui
} // namespace vb
