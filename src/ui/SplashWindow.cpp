#include "ui/SplashWindow.h"

#include "util/Image.h"
#include "util/Log.h"

#include <algorithm>
#include <cstring>

namespace vb {
namespace ui {
namespace {

constexpr wchar_t kSplashClass[] = L"VideoBoothSplashWindow";

} // namespace

SplashWindow::~SplashWindow() {
    Close();
}

bool SplashWindow::Create(HINSTANCE instance, const std::wstring& logoPath,
                          int minimumDurationMs) {
    instance_ = instance;
    minimumDurationMs_ = minimumDurationMs;

    img::Image logo;
    if (!logo.LoadFromFile(logoPath) || !logo.Valid()) {
        VB_WARN("启动画面图片不可用: %ls", logoPath.c_str());
        return false;
    }

    const int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
    int targetWidth = static_cast<int>(screenWidth * 0.34f);
    targetWidth = std::max(320, targetWidth);
    const int targetHeight = std::max(
        1, static_cast<int>(static_cast<float>(logo.height()) * targetWidth / logo.width()));

    if (!canvas_.Resize(targetWidth, targetHeight)) {
        return false;
    }
    canvas_.Clear();
    const RECT dest = {0, 0, targetWidth, targetHeight};
    canvas_.DrawPixels(logo.pixels(), logo.width(), logo.height(), logo.stride(), dest, 0, 255);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSplashClass;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        VB_WARN("注册启动画面窗口类失败");
        return false;
    }

    const int left = (screenWidth - targetWidth) / 2;
    const int top = (::GetSystemMetrics(SM_CYSCREEN) - targetHeight) / 2;
    hwnd_ = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kSplashClass, L"VideoBooth",
                              WS_POPUP, left, top, targetWidth, targetHeight, nullptr, nullptr,
                              instance_, this);
    if (hwnd_ == nullptr) {
        VB_WARN("创建启动画面窗口失败");
        return false;
    }

    HDC screenDc = ::GetDC(nullptr);
    HDC memoryDc = ::CreateCompatibleDC(screenDc);
    HGDIOBJ previous = nullptr;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = targetWidth;
    info.bmiHeader.biHeight = -targetHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(memoryDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap != nullptr && bits != nullptr) {
        std::memcpy(bits, canvas_.pixels(),
                    static_cast<size_t>(canvas_.stride()) * static_cast<size_t>(targetHeight));
        previous = ::SelectObject(memoryDc, bitmap);

        POINT sourcePoint = {0, 0};
        SIZE size = {targetWidth, targetHeight};
        POINT destPoint = {left, top};
        BLENDFUNCTION blend = {};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        ::UpdateLayeredWindow(hwnd_, screenDc, &destPoint, &size, memoryDc, &sourcePoint, 0,
                              &blend, ULW_ALPHA);

        if (previous != nullptr) {
            ::SelectObject(memoryDc, previous);
        }
        ::DeleteObject(bitmap);
    }
    ::DeleteDC(memoryDc);
    ::ReleaseDC(nullptr, screenDc);

    ::ShowWindow(hwnd_, SW_SHOW);
    ::UpdateWindow(hwnd_);
    startTick_ = ::GetTickCount64();
    VB_INFO("启动画面已显示");
    return true;
}

void SplashWindow::Close() {
    if (hwnd_ != nullptr) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (instance_ != nullptr) {
        ::UnregisterClassW(kSplashClass, instance_);
    }
    canvas_.Release();
    // 已关闭时视为展示结束，避免启动流程继续等待
    startTick_ = 0;
}

bool SplashWindow::MinimumElapsed() const {
    if (startTick_ == 0) {
        return true;
    }
    return ::GetTickCount64() - startTick_ >= static_cast<ULONGLONG>(minimumDurationMs_);
}

LRESULT CALLBACK SplashWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace ui
} // namespace vb
