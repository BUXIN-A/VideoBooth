#include "ui/CameraSelectDialog.h"

#include "util/Log.h"

namespace vb {
namespace ui {
namespace {

constexpr wchar_t kDialogClass[] = L"VideoBoothCameraSelectDialog";
constexpr COLORREF kBackgroundColor = RGB(248, 248, 248);

// 背景画刷随进程存活，避免每次弹出对话框泄漏 GDI 对象
HBRUSH BackgroundBrush() {
    static HBRUSH brush = ::CreateSolidBrush(kBackgroundColor);
    return brush;
}

struct LayoutMetrics {
    int clientWidth = 460;
    int clientHeight = 320;
    int margin = 20;
    int labelHeight = 24;
    int listHeight = 196;
    int buttonWidth = 96;
    int buttonHeight = 34;
    int gap = 12;
    int fontPixels = 14;
};

LayoutMetrics MakeMetrics(float scale) {
    LayoutMetrics metrics;
    metrics.clientWidth = static_cast<int>(460.0f * scale);
    metrics.clientHeight = static_cast<int>(320.0f * scale);
    metrics.margin = static_cast<int>(20.0f * scale);
    metrics.labelHeight = static_cast<int>(24.0f * scale);
    metrics.listHeight = static_cast<int>(196.0f * scale);
    metrics.buttonWidth = static_cast<int>(96.0f * scale);
    metrics.buttonHeight = static_cast<int>(34.0f * scale);
    metrics.gap = static_cast<int>(12.0f * scale);
    metrics.fontPixels = static_cast<int>(14.0f * scale);
    return metrics;
}

} // namespace

int CameraSelectDialog::Run(HINSTANCE instance, const std::vector<capture::CameraInfo>& devices) {
    CameraSelectDialog dialog;
    if (!dialog.Create(instance, devices)) {
        return -1;
    }
    const int result = dialog.RunModal();
    dialog.Destroy();
    return result;
}

bool CameraSelectDialog::EnsureClassRegistered(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return true;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = BackgroundBrush();
    wc.lpszClassName = kDialogClass;
    if (::RegisterClassExW(&wc) == 0) {
        VB_ERROR("注册摄像头选择对话框窗口类失败（错误码 %lu）", ::GetLastError());
        return false;
    }
    registered = true;
    return true;
}

bool CameraSelectDialog::Create(HINSTANCE instance,
                                const std::vector<capture::CameraInfo>& devices) {
    instance_ = instance;
    devices_ = devices;

    // 类注册在进程内只做一次，销毁时不再注销，否则第二次弹出会失败
    if (!EnsureClassRegistered(instance_)) {
        return false;
    }

    const UINT dpi = ::GetDpiForSystem();
    const float scale = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    const LayoutMetrics metrics = MakeMetrics(scale);

    RECT windowRect = {0, 0, metrics.clientWidth, metrics.clientHeight};
    ::AdjustWindowRectEx(&windowRect, WS_CAPTION | WS_SYSMENU, FALSE, 0);
    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;

    RECT workArea = {0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const POINT origin = {0, 0};
    const HMONITOR monitor = ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &monitorInfo)) {
        workArea = monitorInfo.rcWork;
    }
    const int left = workArea.left + ((workArea.right - workArea.left) - windowWidth) / 2;
    const int top = workArea.top + ((workArea.bottom - workArea.top) - windowHeight) / 2;

    hwnd_ = ::CreateWindowExW(WS_EX_TOPMOST, kDialogClass, L"选择摄像头",
                              WS_POPUP | WS_CAPTION | WS_SYSMENU, left, top, windowWidth,
                              windowHeight, nullptr, nullptr, instance_, this);
    if (hwnd_ == nullptr) {
        VB_ERROR("创建摄像头选择对话框失败");
        return false;
    }

    ::ShowWindow(hwnd_, SW_SHOW);
    ::SetForegroundWindow(hwnd_);
    return true;
}

void CameraSelectDialog::OnCreate() {
    const UINT dpi = ::GetDpiForWindow(hwnd_);
    const float scale = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    const LayoutMetrics metrics = MakeMetrics(scale);

    font_ = ::CreateFontW(-metrics.fontPixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                          L"Microsoft YaHei UI");

    const int contentWidth = metrics.clientWidth - metrics.margin * 2;
    label_ = ::CreateWindowExW(0, L"STATIC", L"检测到可用的摄像头设备，请选择作为默认展台摄像头：",
                               WS_CHILD | WS_VISIBLE | SS_LEFT, metrics.margin, metrics.margin,
                               contentWidth, metrics.labelHeight, hwnd_, nullptr, instance_,
                               nullptr);

    const int listTop = metrics.margin + metrics.labelHeight + metrics.gap;
    list_ = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY |
                                  LBS_NOINTEGRALHEIGHT,
                              metrics.margin, listTop, contentWidth, metrics.listHeight, hwnd_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(1001)), instance_,
                              nullptr);

    const int buttonTop = listTop + metrics.listHeight + metrics.gap;
    const int confirmLeft = metrics.clientWidth - metrics.margin - metrics.buttonWidth;
    const int cancelLeft = confirmLeft - metrics.gap - metrics.buttonWidth;
    confirm_ = ::CreateWindowExW(0, L"BUTTON", L"确定",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 confirmLeft, buttonTop, metrics.buttonWidth,
                                 metrics.buttonHeight, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), instance_,
                                 nullptr);
    cancel_ = ::CreateWindowExW(0, L"BUTTON", L"取消",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, cancelLeft,
                                buttonTop, metrics.buttonWidth, metrics.buttonHeight, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                                instance_, nullptr);

    ApplyFont(label_);
    ApplyFont(list_);
    ApplyFont(confirm_);
    ApplyFont(cancel_);

    for (const capture::CameraInfo& device : devices_) {
        ::SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(device.name.c_str()));
    }
    if (!devices_.empty()) {
        ::SendMessageW(list_, LB_SETCURSEL, 0, 0);
    }
    ::SetFocus(list_);
}

void CameraSelectDialog::ApplyFont(HWND control) {
    if (control != nullptr && font_ != nullptr) {
        ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
}

void CameraSelectDialog::Confirm(int index) {
    if (index < 0 || index >= static_cast<int>(devices_.size())) {
        return;
    }
    result_ = index;
    if (hwnd_ != nullptr) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    running_ = false;
}

void CameraSelectDialog::Cancel() {
    result_ = -1;
    if (hwnd_ != nullptr) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    running_ = false;
}

int CameraSelectDialog::RunModal() {
    MSG message = {};
    while (running_ && ::GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(hwnd_, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
    return result_;
}

void CameraSelectDialog::Destroy() {
    if (hwnd_ != nullptr) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_ != nullptr) {
        ::DeleteObject(font_);
        font_ = nullptr;
    }
}

LRESULT CALLBACK CameraSelectDialog::WndProc(HWND hwnd, UINT message, WPARAM wParam,
                                             LPARAM lParam) {
    CameraSelectDialog* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<CameraSelectDialog*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
    self = reinterpret_cast<CameraSelectDialog*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->OnMessage(message, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CameraSelectDialog::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_COMMAND: {
        const int controlId = LOWORD(wParam);
        const int notifyCode = HIWORD(wParam);
        if (controlId == 1001 && notifyCode == LBN_DBLCLK) {
            Confirm(static_cast<int>(::SendMessageW(list_, LB_GETCURSEL, 0, 0)));
            return 0;
        }
        if (controlId == IDOK) {
            Confirm(static_cast<int>(::SendMessageW(list_, LB_GETCURSEL, 0, 0)));
            return 0;
        }
        if (controlId == IDCANCEL) {
            Cancel();
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        Cancel();
        return 0;
    case WM_DESTROY:
        running_ = false;
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace ui
} // namespace vb
