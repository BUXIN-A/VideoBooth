#include "ui/MainWindow.h"

#include "annotation/AnnotationFile.h"
#include "core/Paths.h"
#include "resource.h"
#include "ui/FileDialog.h"
#include "ui/SettingsDialog.h"
#include "util/Image.h"
#include "util/Log.h"
#include "util/Strings.h"

#include <windows.h>
#include <windowsx.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vb {
namespace ui {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT_PTR kRenderTimer = 1;
constexpr UINT kRenderTimerIntervalMs = 5;
// 绘制节流：避免在无垂直同步时空转烧掉低配设备性能
constexpr ULONGLONG kMinPresentIntervalMs = 15;
constexpr ULONGLONG kCameraMaintainIntervalMs = 2000;
constexpr ULONGLONG kPreviewRefreshIntervalMs = 45;
constexpr ULONGLONG kToastDurationMs = 2000;
constexpr int kPhotoJpegQuality = 85;

const gfx::Color kBackdrop(0.07f, 0.07f, 0.07f, 1.0f);

POINT CenterPoint(const RECT& rect) {
    POINT point = {};
    point.x = rect.left + (rect.right - rect.left) / 2;
    point.y = rect.top + (rect.bottom - rect.top) / 2;
    return point;
}

// 目标文件已存在时追加序号，避免导入/批量保存时相互覆盖
std::wstring UniqueTargetPath(const std::wstring& path) {
    if (!paths::FileExists(path)) {
        return path;
    }
    const size_t dot = path.find_last_of(L'.');
    const std::wstring stem = dot == std::wstring::npos ? path : path.substr(0, dot);
    const std::wstring extension = dot == std::wstring::npos ? std::wstring() : path.substr(dot);
    for (int i = 1; i < 10000; ++i) {
        const std::wstring candidate = stem + L"_" + std::to_wstring(i) + extension;
        if (!paths::FileExists(candidate)) {
            return candidate;
        }
    }
    return path;
}

// 触摸被系统合成为鼠标消息时会带触摸签名（0xFF515700，bit7 置位表示触摸源）。
// 触摸统一由 WM_POINTER 处理，这里过滤掉合成消息，避免一次触摸被响应两次。
bool IsTouchSynthesizedMouseMessage(UINT message) {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        break;
    default:
        return false;
    }
    constexpr unsigned long long kSignatureMask = 0xFFFFFF00ull;
    constexpr unsigned long long kTouchSignature = 0xFF515700ull;
    const unsigned long long extra = static_cast<unsigned long long>(::GetMessageExtraInfo());
    return (extra & kSignatureMask) == kTouchSignature && (extra & 0x80ull) != 0;
}

// 导入的图片统一转存为 JPG（相册只收录 IMG_*.jpg）；.jpg/.jpeg 直接复制避免二次压缩
bool ImportPhotoFile(const std::wstring& source, const std::wstring& directory) {
    SYSTEMTIME time = {};
    ::GetLocalTime(&time);
    const std::wstring stemPath = paths::JoinPath(directory, paths::PhotoFileName(time));

    const size_t dot = source.find_last_of(L'.');
    const std::wstring extension = dot == std::wstring::npos ? std::wstring() : source.substr(dot);
    if (EqualsIgnoreCase(extension, L".jpg") || EqualsIgnoreCase(extension, L".jpeg")) {
        const std::wstring target = UniqueTargetPath(stemPath);
        if (!::CopyFileW(source.c_str(), target.c_str(), TRUE)) {
            VB_WARN("导入照片失败（错误码 %lu）: %ls", ::GetLastError(), source.c_str());
            return false;
        }
        VB_INFO("照片已导入: %ls → %ls", source.c_str(), target.c_str());
        return true;
    }

    img::Image image;
    if (!image.LoadFromFile(source)) {
        VB_WARN("导入照片解码失败: %ls", source.c_str());
        return false;
    }
    const std::wstring target = UniqueTargetPath(stemPath);
    if (!img::SaveJpeg(target, image.pixels(), image.width(), image.height(), image.stride(),
                       kPhotoJpegQuality)) {
        VB_WARN("导入照片转存失败: %ls", source.c_str());
        return false;
    }
    VB_INFO("照片已导入并转存为 JPG: %ls → %ls", source.c_str(), target.c_str());
    return true;
}

} // namespace

MainWindow::~MainWindow() {
    Destroy();
}

bool MainWindow::Create(HINSTANCE instance, const MainWindowDeps& deps) {
    instance_ = instance;
    deps_ = deps;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hIcon = ::LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMainWindowClassName;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        VB_ERROR("注册主窗口类失败");
        return false;
    }

    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const POINT origin = {0, 0};
    RECT area = {0, 0, 0, 0};
    const HMONITOR monitor = ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &monitorInfo)) {
        // 展台为全屏窗口：覆盖整个显示器（含任务栏区域）
        area = monitorInfo.rcMonitor;
    }

    int areaWidth = static_cast<int>(area.right - area.left);
    int areaHeight = static_cast<int>(area.bottom - area.top);
    if (areaWidth <= 0 || areaHeight <= 0) {
        area = {0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
        areaWidth = area.right;
        areaHeight = area.bottom;
        VB_WARN("无法获取显示器尺寸，回退为屏幕尺寸 %dx%d", areaWidth, areaHeight);
    }
    if (areaWidth <= 0 || areaHeight <= 0) {
        area = {0, 0, 1280, 720};
        areaWidth = 1280;
        areaHeight = 720;
        VB_WARN("无法获取屏幕尺寸，回退为 %dx%d", areaWidth, areaHeight);
    }

    hwnd_ = ::CreateWindowExW(WS_EX_APPWINDOW, kMainWindowClassName, L"VideoBooth 视频展台",
                              WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, area.left, area.top,
                              areaWidth, areaHeight, nullptr, nullptr, instance_, this);
    if (hwnd_ == nullptr) {
        VB_ERROR("创建主窗口失败，错误码 %lu（区域 %d,%d %dx%d）", ::GetLastError(),
                 static_cast<int>(area.left), static_cast<int>(area.top), areaWidth, areaHeight);
        return false;
    }
    VB_INFO("主窗口已创建（全屏）: %dx%d", areaWidth, areaHeight);

    const UINT dpi = ::GetDpiForWindow(hwnd_);
    uiScale_ = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    VB_INFO("主窗口 UI 缩放比例: %.2f (DPI %u)", uiScale_, dpi);

    toolbar_.Init(deps_.resources, deps_.configStore->Get().IsToolbarVertical());
    toolbar_.SetScale(uiScale_);
    preview_.Init();
    preview_.SetScale(uiScale_);
    morePanel_.Init(deps_.resources);
    morePanel_.SetScale(uiScale_);
    albumPanel_.Init(deps_.resources);
    albumPanel_.SetScale(uiScale_);
    SyncAnnotationStyle();

    if (!gl_.Create(hwnd_, deps_.configStore->Get().render.vsync,
                    deps_.configStore->Get().render.doubleBuffer,
                    deps_.configStore->Get().render.antialias)) {
        ::MessageBoxW(hwnd_, L"初始化 OpenGL 渲染环境失败，程序无法继续运行。", L"VideoBooth",
                      MB_ICONERROR | MB_OK);
        return false;
    }
    if (!renderer_.Init()) {
        ::MessageBoxW(hwnd_, L"初始化 OpenGL 着色器失败，程序无法继续运行。", L"VideoBooth",
                      MB_ICONERROR | MB_OK);
        return false;
    }

    gl_.MakeCurrent();
    Relayout();
    SyncToolbarState();
    ShowToast(L"正在初始化画面…");

    saveQueue_.Start(); // 后台 JPG 保存线程

    ::ShowWindow(hwnd_, SW_SHOW);
    ::SetForegroundWindow(hwnd_);
    ::SetTimer(hwnd_, kRenderTimer, kRenderTimerIntervalMs, nullptr);
    return true;
}

void MainWindow::Destroy() {
    if (hwnd_ != nullptr) {
        ::KillTimer(hwnd_, kRenderTimer);
        saveQueue_.Stop(); // 等待已提交的 JPG 保存完成，避免残留半写文件
        if (gl_.IsValid()) {
            gl_.MakeCurrent();
            pictureTexture_.Destroy();
            annotationTexture_.Destroy();
            toolbarTexture_.Destroy();
            previewTexture_.Destroy();
            toastTexture_.Destroy();
            morePanelTexture_.Destroy();
            albumPanelTexture_.Destroy();
            settingsTexture_.Destroy();
            eraserTexture_.Destroy();
            renderer_.Shutdown();
            gl_.Destroy();
        }
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (instance_ != nullptr) {
        ::UnregisterClassW(kMainWindowClassName, instance_);
    }
    currentFrame_.reset();
}

void MainWindow::RequestClose() {
    if (hwnd_ != nullptr) {
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

void MainWindow::RunMessageLoop() {
    MSG message = {};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        // 创建期消息必须交由默认过程处理，WM_NCCREATE 返回 FALSE 会导致创建失败
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    auto* self = reinterpret_cast<MainWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    // 仅在窗口句柄已登记后才交由实例处理，避免创建期间使用未初始化的成员
    if (self != nullptr && self->hwnd_ == hwnd) {
        return self->OnMessage(message, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    // 触摸已被系统合成为鼠标消息时直接忽略：触摸统一由 WM_POINTER 处理
    if (IsTouchSynthesizedMouseMessage(message)) {
        return 0;
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        ::ValidateRect(hwnd_, nullptr);
        return 0;
    }
    case WM_SIZE: {
        if (wParam == SIZE_MINIMIZED) {
            EnterMinimized();
        } else {
            if (minimized_) {
                LeaveMinimized();
            }
            clientWidth_ = LOWORD(lParam);
            clientHeight_ = HIWORD(lParam);
            Relayout();
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == kRenderTimer) {
            OnTimer();
        }
        return 0;
    case WM_MOUSEMOVE: {
        DispatchPointerMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    }
    case WM_MOUSELEAVE:
        if (settingsDialog_.isOpen()) {
            const POINT outside = {-1, -1};
            settingsDialog_.OnMouseMove(outside);
            return 0;
        }
        OnMouseLeave();
        return 0;
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    case WM_POINTERENTER:
    case WM_POINTERLEAVE:
    case WM_POINTERCAPTURECHANGED:
        // 触摸自行处理并消费，避免系统再合成为鼠标消息造成一次触摸响应两次
        if (HandlePointerMessage(message, wParam)) {
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        DispatchPointerDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
        return 0;
    case WM_MBUTTONDOWN:
        DispatchPointerDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
        DispatchPointerUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ::ScreenToClient(hwnd_, &point);
        if (settingsDialog_.isOpen()) {
            settingsDialog_.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), point);
            return 0;
        }
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), point.x, point.y);
        return 0;
    }
    case WM_KEYDOWN:
        if (settingsDialog_.isOpen()) {
            settingsDialog_.OnKeyDown(static_cast<UINT>(wParam));
            if (!settingsDialog_.isOpen()) {
                FinishSettingsDialog();
            }
            return 0;
        }
        OnKeyDown(static_cast<UINT>(wParam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            EnterMinimized();
            return 0;
        }
        break;
    case WM_CLOSE:
        if (deps_.onExitRequested) {
            deps_.onExitRequested();
        }
        // 交由默认处理执行 DestroyWindow，消息循环随后结束
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd_, message, wParam, lParam);
}

void MainWindow::OnTimer() {
    const ULONGLONG now = ::GetTickCount64();
    // 设置面板展示期间画面不变化，仅在面板需要重绘时才出帧，节省后台性能
    const bool needPresent = !settingsOpen_ || settingsDialog_.dirty();
    if (!minimized_ && needPresent && now - lastPresentTick_ >= kMinPresentIntervalMs) {
        lastPresentTick_ = now;
        OnRender();
    }

    // 后台保存完成后刷新相册：等队列排空再刷新，避免读到写了一半的照片
    const unsigned long long completed = saveQueue_.completedCount();
    if (completed != lastSaveCompleted_ && saveQueue_.pendingCount() == 0) {
        lastSaveCompleted_ = completed;
        RefreshAlbumPanelIfOpen();
    }

    if (deps_.camera != nullptr && !deps_.camera->IsOpen() &&
        now - lastCameraMaintain_ >= kCameraMaintainIntervalMs) {
        lastCameraMaintain_ = now;
        if (deps_.onCameraMaintain) {
            deps_.onCameraMaintain();
        }
        // 摄像头热插拔重连后，按当前状态决定是否拉流（锁定/查看照片时不采集）
        if (deps_.camera->IsOpen()) {
            deps_.camera->SetActive(ShouldCapture());
        }
    }
}

void MainWindow::OnRender() {
    if (!gl_.IsValid() || hwnd_ == nullptr) {
        return;
    }
    RECT client = {};
    ::GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    gl_.MakeCurrent();
    renderer_.BeginFrame(width, height, kBackdrop);

    UpdatePicture();

    if (picturePixels_ != nullptr && pictureWidth_ > 0 && pictureHeight_ > 0) {
        if (pictureDirty_) {
            pictureTexture_.Upload(picturePixels_, pictureWidth_, pictureHeight_, pictureStride_);
            pictureDirty_ = false;
        }
        if (pictureTexture_.valid()) {
            // dest 使用未旋转的画面尺寸；GL 绘制时绕中心旋转，旋转后占位自然变成宽高互换，
            // 从而保证画面比例不变（不拉伸）
            const float scale = ComputeScale();
            const float drawWidth = pictureWidth_ * scale;
            const float drawHeight = pictureHeight_ * scale;
            const float centerX = width * 0.5f + state_.offsetX;
            const float centerY = height * 0.5f + state_.offsetY;
            const RECT dest = {static_cast<LONG>(centerX - drawWidth * 0.5f),
                               static_cast<LONG>(centerY - drawHeight * 0.5f),
                               static_cast<LONG>(centerX + drawWidth * 0.5f),
                               static_cast<LONG>(centerY + drawHeight * 0.5f)};
            const float rotation = static_cast<float>(state_.rotationQuarter) * 90.0f;
            renderer_.DrawTexture(pictureTexture_, dest, rotation);

            // 批注层与画面使用完全相同的变换，因此笔迹随旋转/缩放/拖动一起变化
            // （实时画面与相册照片各自持有独立笔迹层）
            if (annotation_.valid() && annotation_.width() == pictureWidth_ &&
                annotation_.height() == pictureHeight_) {
                RECT dirty = {};
                if (annotation_.TakeDirtyRect(dirty)) {
                    annotationTexture_.UploadRegion(annotation_.pixels(), annotation_.width(),
                                                    annotation_.height(), annotation_.stride(),
                                                    dirty);
                }
                if (annotationTexture_.valid()) {
                    renderer_.DrawTexture(annotationTexture_, dest, rotation);
                }
            }
        }
    }

    // 橡皮模式：绘制圆形擦除范围（触摸不移动系统鼠标指针，需用接触点位置）
    if (state_.tool == core::ToolMode::Erase && state_.pictureAnnotatable()) {
        if (annotating_ && !touchContacts_.empty()) {
            const POINT point = touchContacts_.front().pos;
            cursorPos_ = point;
            cursorInside_ = point.x >= 0 && point.y >= 0 && point.x < width && point.y < height;
        } else {
            POINT cursor = {};
            cursorInside_ =
                ::GetCursorPos(&cursor) != FALSE && ::ScreenToClient(hwnd_, &cursor) != FALSE;
            if (cursorInside_) {
                cursorPos_ = cursor;
                cursorInside_ =
                    cursor.x >= 0 && cursor.y >= 0 && cursor.x < width && cursor.y < height;
            }
        }
        UpdateEraserCursor();
        if (EraserCursorVisible()) {
            const int size = eraserTexture_.width();
            const int half = size / 2;
            const RECT dest = {cursorPos_.x - half, cursorPos_.y - half, cursorPos_.x - half + size,
                               cursorPos_.y - half + size};
            renderer_.DrawTexture(eraserTexture_, dest);
        }
    }

    UpdatePreview();
    UploadOverlayTextures();

    if (previewTexture_.valid()) {
        renderer_.DrawTexture(previewTexture_, preview_.bounds());
    }
    if (morePanel_.modeVisible() && morePanelTexture_.valid()) {
        renderer_.DrawTexture(morePanelTexture_, morePanel_.bounds());
    }
    if (toolbarTexture_.valid()) {
        renderer_.DrawTexture(toolbarTexture_, toolbar_.bounds());
    }
    if (albumPanel_.isOpen() && albumPanelTexture_.valid()) {
        renderer_.DrawTexture(albumPanelTexture_, albumPanel_.bounds());
    }

    const ULONGLONG now = ::GetTickCount64();
    if (!toastText_.empty() && now < toastUntil_ && toastTexture_.valid()) {
        renderer_.DrawTexture(toastTexture_, toastRect_);
    } else if (!toastText_.empty() && now >= toastUntil_) {
        toastText_.clear();
    }

    // 设置面板绘制在最上层，保证全屏展台下始终可见
    UploadSettingsTexture();
    if (settingsDialog_.isOpen() && settingsTexture_.valid()) {
        renderer_.DrawTexture(settingsTexture_, settingsDialog_.bounds());
    }

    gl_.Present();

    if (!firstFrameReported_) {
        firstFrameReported_ = true;
        const RECT& bar = toolbar_.bounds();
        const RECT& panel = preview_.bounds();
        VB_INFO("首帧渲染完成: 客户区 %dx%d, 画面纹理 %dx%d, 功能栏 %d,%d-%d,%d, 预览框 "
                "%d,%d-%d,%d, GL 错误=0x%04X",
                width, height, pictureTexture_.width(), pictureTexture_.height(), bar.left,
                bar.top, bar.right, bar.bottom, panel.left, panel.top, panel.right,
                panel.bottom, static_cast<unsigned int>(::glGetError()));
    }
}

float MainWindow::ComputeScale() const {
    if (pictureWidth_ <= 0 || pictureHeight_ <= 0 || clientWidth_ <= 0 ||
        clientHeight_ <= 0) {
        return 1.0f;
    }
    // 始终按未旋转尺寸适配窗口：旋转时保持原比例直接旋转，不再重新拉伸画面
    const float fit = std::min(static_cast<float>(clientWidth_) / pictureWidth_,
                               static_cast<float>(clientHeight_) / pictureHeight_);
    return std::max(0.01f, fit * state_.zoom);
}

void MainWindow::UpdatePicture() {
    // 展示相册照片期间画面固定为照片，不取摄像头帧
    if (AlbumPhotoShown()) {
        return;
    }

    const bool cameraOpen = deps_.camera != nullptr && deps_.camera->IsOpen();
    state_.cameraAvailable = cameraOpen;

    if (state_.locked) {
        // 锁定状态：保持最后一帧，不再从摄像头取帧
        if (picturePixels_ == nullptr) {
            SetErrorPicture();
        } else {
            // 锁定期间画面归属也可能变化（如查看照片后返回相机），
            // 需保证笔迹层属于当前画面，否则锁定状态下无法批注
            EnsureAnnotationFor(CurrentAnnotationKey(), pictureWidth_, pictureHeight_);
        }
        return;
    }

    if (cameraOpen) {
        capture::FramePtr frame = deps_.camera->TakeLatest();
        if (frame && frame->index != lastFrameIndex_ && !frame->pixels.empty()) {
            lastFrameIndex_ = frame->index;
            currentFrame_ = frame;
            picturePixels_ = frame->pixels.data();
            pictureWidth_ = frame->width;
            pictureHeight_ = frame->height;
            pictureStride_ = frame->stride;
            pictureDirty_ = true;
            state_.picture = core::PictureSource::Camera;
            EnsureAnnotationFor(CurrentAnnotationKey(), pictureWidth_, pictureHeight_);
            // 画面已改用新帧，相册照片缓冲不再被引用，及时释放
            if (albumImage_.Valid()) {
                albumImage_.Reset();
            }
        }
        return;
    }

    if (state_.picture != core::PictureSource::Error) {
        SetErrorPicture();
    }
}

std::wstring MainWindow::CurrentAnnotationKey() const {
    // 正在全屏查看照片时，笔迹归属于该照片；其余情况统一归属实时画面
    return AlbumPhotoShown() ? albumShownName_ : std::wstring();
}

std::wstring MainWindow::AnnotationPath(const std::wstring& key) const {
    return annotation::AnnotationPathFor(PhotoDirectory(), key);
}

void MainWindow::SaveAnnotation() const {
    const std::wstring path = AnnotationPath(annotationKey_);
    if (path.empty()) {
        return;
    }
    annotation::SaveToFile(path, annotation_);
}

void MainWindow::ReleaseAnnotation() {
    SaveAnnotation();
    annotation_.Reset(0, 0);
    annotationTexture_.Destroy();
    annotationKey_.clear();
    annotating_ = false;
    eraserCursorDiameter_ = 0;
}

void MainWindow::EnsureAnnotationFor(const std::wstring& key, int imageWidth, int imageHeight) {
    if (key == annotationKey_ && annotation_.width() == imageWidth &&
        annotation_.height() == imageHeight && annotation_.valid()) {
        return; // 已是该图片的笔迹层
    }
    // 归属或尺寸变化：先把旧笔迹写回其文件，再切换到目标图片
    if (annotation_.valid() || !annotationKey_.empty()) {
        SaveAnnotation();
    }
    annotationKey_ = key;
    annotation_.Reset(0, 0);
    annotationTexture_.Destroy();
    annotating_ = false;
    eraserCursorDiameter_ = 0;

    if (imageWidth <= 0 || imageHeight <= 0 || !state_.pictureAnnotatable()) {
        return;
    }
    annotation_.Reset(imageWidth, imageHeight);
    SyncAnnotationStyle();
    // 已有笔迹文件时恢复（尺寸不符会被忽略，保持空白层）
    annotation::LoadFromFile(AnnotationPath(key), annotation_, imageWidth, imageHeight);
    VB_INFO("笔迹层已切换到 %ls（%dx%d，笔迹=%s）", key.empty() ? L"实时画面" : key.c_str(),
            imageWidth, imageHeight, annotation_.empty() ? "无" : "有");
}

void MainWindow::SyncAnnotationStyle() {
    annotation_.SetColor(morePanel_.color());
    annotation_.SetThickness(morePanel_.penThickness());
    annotation_.SetEraserRadius(morePanel_.eraserRadius());
}

void MainWindow::UpdateEraserCursor() {
    if (state_.tool != core::ToolMode::Erase || !state_.pictureAnnotatable() ||
        !annotation_.valid()) {
        return;
    }
    // 橡皮半径是图像像素，换算到屏幕上才是实际擦除面积
    const int diameter =
        static_cast<int>(annotation_.eraserRadius() * 2.0f * ComputeScale() + 0.5f);
    if (diameter < 6) {
        return;
    }
    if (diameter == eraserCursorDiameter_ && eraserTexture_.valid()) {
        return;
    }

    // 圆环纹理：中间白环 + 内外黑色描边，保证在深色与浅色画面上都清晰
    const int size = diameter + 6;
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u, 0);
    const float center = static_cast<float>(size) * 0.5f;
    const float radius = static_cast<float>(diameter) * 0.5f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - center;
            const float dy = static_cast<float>(y) + 0.5f - center;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float black = std::min(1.0f, std::max(0.0f, radius + 2.5f - distance)) *
                                std::min(1.0f, std::max(0.0f, distance - (radius - 2.5f)));
            const float white = std::min(1.0f, std::max(0.0f, radius + 1.2f - distance)) *
                                std::min(1.0f, std::max(0.0f, distance - (radius - 1.2f)));
            const float alpha = std::max(white, black * 0.7f);
            if (alpha <= 0.0f) {
                continue;
            }
            const float value = white > 0.0f ? 1.0f : 0.0f;
            uint8_t* pixel = pixels.data() + (static_cast<size_t>(y) * size + x) * 4u;
            pixel[0] = static_cast<uint8_t>(value * alpha * 255.0f + 0.5f);
            pixel[1] = pixel[0];
            pixel[2] = pixel[0];
            pixel[3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
        }
    }
    if (eraserTexture_.Upload(pixels.data(), size, size, size * 4)) {
        eraserCursorDiameter_ = diameter;
    }
}

bool MainWindow::EraserCursorVisible() const {
    // 仅在按住左键擦除时显示圆形范围指示
    if (!cursorInside_ || !annotating_ || state_.tool != core::ToolMode::Erase ||
        !state_.pictureAnnotatable() || !eraserTexture_.valid()) {
        return false;
    }
    const POINT point = cursorPos_;
    const auto insideRect = [&point](const RECT& rect) {
        return point.x >= rect.left && point.x < rect.right && point.y >= rect.top &&
               point.y < rect.bottom;
    };
    if (toolbar_.HitTest(point) >= 0) {
        return false;
    }
    if (morePanel_.modeVisible() &&
        (morePanel_.ContainsPoint(point) || insideRect(morePanel_.tabBounds()))) {
        return false;
    }
    if (insideRect(preview_.bounds())) {
        return false;
    }
    return true;
}

void MainWindow::WindowToImage(int windowX, int windowY, float* imageX, float* imageY) const {
    if (imageX != nullptr) {
        *imageX = 0.0f;
    }
    if (imageY != nullptr) {
        *imageY = 0.0f;
    }
    if (pictureWidth_ <= 0 || pictureHeight_ <= 0 || clientWidth_ <= 0 || clientHeight_ <= 0) {
        return;
    }
    const float scale = ComputeScale();
    if (scale <= 0.0001f) {
        return;
    }
    // 窗口坐标 → 旋转后画面局部坐标
    const float localX = (static_cast<float>(windowX) -
                          (clientWidth_ * 0.5f + state_.offsetX)) / scale;
    const float localY = (static_cast<float>(windowY) -
                          (clientHeight_ * 0.5f + state_.offsetY)) / scale;

    // 旋转后坐标 → 原始图像坐标（与渲染时的顺时针旋转对应）
    const float halfWidth = pictureWidth_ * 0.5f;
    const float halfHeight = pictureHeight_ * 0.5f;
    float imagePointX = 0.0f;
    float imagePointY = 0.0f;
    switch (state_.rotationQuarter & 3) {
    case 1:
        imagePointX = localY + halfWidth;
        imagePointY = -localX + halfHeight;
        break;
    case 2:
        imagePointX = -localX + halfWidth;
        imagePointY = -localY + halfHeight;
        break;
    case 3:
        imagePointX = -localY + halfWidth;
        imagePointY = localX + halfHeight;
        break;
    default:
        imagePointX = localX + halfWidth;
        imagePointY = localY + halfHeight;
        break;
    }
    if (imageX != nullptr) {
        *imageX = imagePointX;
    }
    if (imageY != nullptr) {
        *imageY = imagePointY;
    }
}

void MainWindow::SetErrorPicture() {
    currentFrame_.reset();
    // 无摄像头时 error.png 不可批注，笔迹先写回文件再释放
    ReleaseAnnotation();
    if (deps_.errorImage != nullptr && deps_.errorImage->Valid()) {
        picturePixels_ = deps_.errorImage->pixels();
        pictureWidth_ = deps_.errorImage->width();
        pictureHeight_ = deps_.errorImage->height();
        pictureStride_ = deps_.errorImage->stride();
        pictureDirty_ = true;
        state_.picture = core::PictureSource::Error;
        // 无摄像头时 error.png 不可缩放、旋转
        state_.zoom = 1.0f;
        state_.offsetX = 0.0f;
        state_.offsetY = 0.0f;
        state_.rotationQuarter = 0;
        ShowToast(L"无摄像头可用");
    } else {
        picturePixels_ = nullptr;
        pictureWidth_ = 0;
        pictureHeight_ = 0;
        pictureStride_ = 0;
        pictureDirty_ = false;
        state_.picture = core::PictureSource::None;
    }
    VB_INFO("画面切换为无摄像头占位图");
}

void MainWindow::UpdatePreview() {
    const ULONGLONG now = ::GetTickCount64();
    if (now - lastPreviewRefresh_ < kPreviewRefreshIntervalMs) {
        return;
    }
    lastPreviewRefresh_ = now;

    PreviewView view;
    view.pixels = picturePixels_;
    view.imageWidth = pictureWidth_;
    view.imageHeight = pictureHeight_;
    view.stride = pictureStride_;
    view.rotationQuarter = state_.rotationQuarter;
    view.windowWidth = clientWidth_;
    view.windowHeight = clientHeight_;
    view.scale = ComputeScale();
    view.offsetX = state_.offsetX;
    view.offsetY = state_.offsetY;
    view.interactive = state_.pictureZoomable();
    if (annotation_.valid() && annotation_.width() == pictureWidth_ &&
        annotation_.height() == pictureHeight_) {
        view.overlayPixels = annotation_.pixels();
        view.overlayWidth = annotation_.width();
        view.overlayHeight = annotation_.height();
        view.overlayStride = annotation_.stride();
        view.overlayVersion = annotation_.version();
    }
    preview_.SetView(view);

    if (preview_.Update()) {
        const OverlayCanvas& canvas = preview_.canvas();
        previewTexture_.Upload(canvas.pixels(), canvas.width(), canvas.height(), canvas.stride());
    }
}

void MainWindow::UploadOverlayTextures() {
    if (toolbarDirty_) {
        toolbar_.Render(hoverIndex_, pressedIndex_);
        const OverlayCanvas& canvas = toolbar_.canvas();
        if (canvas.valid()) {
            toolbarTexture_.Upload(canvas.pixels(), canvas.width(), canvas.height(),
                                   canvas.stride());
        }
        toolbarDirty_ = false;
    }

    if (morePanelDirty_) {
        morePanel_.Render();
        const OverlayCanvas& canvas = morePanel_.canvas();
        if (canvas.valid()) {
            morePanelTexture_.Upload(canvas.pixels(), canvas.width(), canvas.height(),
                                     canvas.stride());
        }
        morePanelDirty_ = false;
    }

    UploadAlbumPanelTexture();
}

void MainWindow::UploadAlbumPanelTexture() {
    if (!albumPanelDirty_) {
        return;
    }
    albumPanelDirty_ = false;
    if (!albumPanel_.isOpen()) {
        albumPanelTexture_.Destroy();
        return;
    }
    albumPanel_.Render([this](size_t index) { return photoLibrary_.Thumbnail(index); });
    const OverlayCanvas& canvas = albumPanel_.canvas();
    if (canvas.valid()) {
        albumPanelTexture_.Upload(canvas.pixels(), canvas.width(), canvas.height(),
                                  canvas.stride());
    }
}

void MainWindow::UploadSettingsTexture() {
    if (!settingsDialog_.isOpen() || !settingsDialog_.dirty()) {
        return;
    }
    settingsDialog_.Render();
    const OverlayCanvas& canvas = settingsDialog_.canvas();
    if (canvas.valid()) {
        settingsTexture_.Upload(canvas.pixels(), canvas.width(), canvas.height(), canvas.stride());
    }
    settingsDialog_.ClearDirty();
}

void MainWindow::Relayout() {
    if (clientWidth_ <= 0 || clientHeight_ <= 0) {
        RECT client = {};
        ::GetClientRect(hwnd_, &client);
        clientWidth_ = client.right;
        clientHeight_ = client.bottom;
    }
    toolbar_.Layout(clientWidth_, clientHeight_);
    preview_.Layout(clientHeight_);
    // “更多”标签贴在所选模式按钮外侧（底部功能栏为正上方，两侧功能栏为左侧）
    const int anchorIndex = state_.tool == core::ToolMode::Erase
                                ? static_cast<int>(ToolButtonId::Erase)
                                : static_cast<int>(ToolButtonId::Annotate);
    morePanel_.Layout(toolbar_.bounds(), toolbar_.buttonRect(anchorIndex), toolbar_.vertical(),
                      clientWidth_, clientHeight_);
    albumPanel_.Layout(toolbar_.bounds(), clientWidth_, clientHeight_);
    if (settingsDialog_.isOpen()) {
        settingsDialog_.Relayout(clientWidth_, clientHeight_);
    }
    // 窗口尺寸变化后位移上限随之变化，需重新收敛
    ClampOffsets();
    toolbarDirty_ = true;
    morePanelDirty_ = true;
    albumPanelDirty_ = true;
}

void MainWindow::SyncToolbarState() {
    const bool albumPanelOpen = albumPanel_.isOpen();
    toolbar_.SetActive(static_cast<int>(ToolButtonId::Select),
                       state_.tool == core::ToolMode::Select && state_.view == core::ViewMode::Live &&
                           !albumPanelOpen);
    toolbar_.SetActive(static_cast<int>(ToolButtonId::Annotate),
                       state_.tool == core::ToolMode::Annotate);
    toolbar_.SetActive(static_cast<int>(ToolButtonId::Erase),
                       state_.tool == core::ToolMode::Erase);
    toolbar_.SetActive(static_cast<int>(ToolButtonId::Lock), state_.locked);
    // 相册按钮：面板展开或正在查看照片时高亮
    toolbar_.SetActive(static_cast<int>(ToolButtonId::Album),
                       albumPanelOpen || AlbumPhotoShown());
    // 相册面板与照片查看时功能栏仍可用，仅设置框期间禁用
    toolbar_.SetEnabled(state_.view != core::ViewMode::Settings);
    // 仅在全屏查看照片时，“拍照”变为“返回相机”
    if (AlbumPhotoShown()) {
        toolbar_.SetButtonContent(ToolButtonId::Shoot, L"shoot.png", L"返回相机");
    } else {
        toolbar_.ClearButtonContent(ToolButtonId::Shoot);
    }
    toolbarDirty_ = true;
}

void MainWindow::OnMouseMove(int x, int y) {
    POINT point = {x, y};

    // 相册面板：拖动滚动与按钮悬停优先
    if (albumPanel_.IsDragging()) {
        albumPanel_.DragTo(x);
        albumPanelDirty_ = true;
        return;
    }
    if (albumPanel_.isOpen()) {
        const AlbumPanelHit hit = albumPanel_.HitTest(point);
        const AlbumPanelHit& previous = albumPanel_.hover();
        if (hit.kind != previous.kind || hit.index != previous.index) {
            albumPanel_.SetHover(hit);
            albumPanelDirty_ = true;
        }
        if (albumPanel_.ContainsPoint(point)) {
            return;
        }
    }

    // 粗细滑块拖动：按住时连续跟随鼠标
    if (moreSliderDrag_) {
        if (morePanel_.PenSliderFromPoint(point)) {
            SyncAnnotationStyle();
            morePanelDirty_ = true;
        }
        return;
    }

    if (annotating_) {
        float imageX = 0.0f;
        float imageY = 0.0f;
        WindowToImage(x, y, &imageX, &imageY);
        annotation_.PointerMove(imageX, imageY);
        return;
    }

    if (draggingView_) {
        const int deltaX = x - lastMouse_.x;
        const int deltaY = y - lastMouse_.y;
        state_.offsetX += static_cast<float>(deltaX);
        state_.offsetY += static_cast<float>(deltaY);
        ClampOffsets();
        lastMouse_ = point;
        return;
    }
    if (draggingViewport_) {
        const int deltaX = x - lastMouse_.x;
        const int deltaY = y - lastMouse_.y;
        float offsetDeltaX = 0.0f;
        float offsetDeltaY = 0.0f;
        preview_.MapDragToOffsetDelta(static_cast<float>(deltaX), static_cast<float>(deltaY),
                                      &offsetDeltaX, &offsetDeltaY);
        state_.offsetX += offsetDeltaX;
        state_.offsetY += offsetDeltaY;
        ClampOffsets();
        lastMouse_ = point;
        return;
    }

    const int hover = toolbar_.HitTest(point);
    if (hover != hoverIndex_) {
        hoverIndex_ = hover;
        toolbarDirty_ = true;
    }

    TRACKMOUSEEVENT track = {};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = hwnd_;
    ::TrackMouseEvent(&track);
}

void MainWindow::OnMouseLeave() {
    if (hoverIndex_ != -1) {
        hoverIndex_ = -1;
        toolbarDirty_ = true;
    }
}

void MainWindow::OnButtonDown(int x, int y, bool middle) {
    POINT point = {x, y};
    lastMouse_ = point;
    ::SetCapture(hwnd_);

    if (!middle) {
        // 0. 相册面板展开时优先处理：卡片按钮、缩略图点击与拖动滚动
        if (albumPanel_.isOpen() && albumPanel_.ContainsPoint(point)) {
            const AlbumPanelHit hit = albumPanel_.HitTest(point);
            albumPressedHit_ = hit;
            if (hit.kind == AlbumPanelHit::Kind::Thumbnail ||
                hit.kind == AlbumPanelHit::Kind::None) {
                albumPanel_.BeginDrag(x);
            }
            albumPanelPressed_ = true;
            albumPanelDirty_ = true;
            return;
        }

        // 1. “更多”面板展开时优先处理其内容
        if (morePanel_.isOpen()) {
            const MorePanelHit hit = morePanel_.HitTest(point);
            if (hit.kind != MorePanelHit::Kind::None) {
                HandleMorePanelHit(hit);
                // 粗细滑块支持按住拖动连续调整
                if (hit.kind == MorePanelHit::Kind::PenThickness) {
                    moreSliderDrag_ = true;
                }
                return;
            }
            if (morePanel_.ContainsPoint(point)) {
                return;
            }
        }

        // 2. “更多”标签（批注/橡皮模式）
        if (morePanel_.modeVisible()) {
            const RECT tab = morePanel_.tabBounds();
            if (point.x >= tab.left && point.x < tab.right && point.y >= tab.top &&
                point.y < tab.bottom) {
                morePanel_.SetOpen(!morePanel_.isOpen());
                morePanelDirty_ = true;
                return;
            }
        }

        // 3. 功能栏
        const int index = toolbar_.HitTest(point);
        if (index >= 0) {
            pressedIndex_ = index;
            toolbarDirty_ = true;
            return;
        }

        // 4. 点击“更多”面板、其标签与功能栏以外的空白区域：收起“更多”面板
        if (morePanel_.isOpen()) {
            morePanel_.SetOpen(false);
            morePanelDirty_ = true;
            return;
        }

        // 5. 相册面板展开时，点击面板与功能栏以外的空白区域收起面板；
        //    仅收起面板，正在全屏查看的照片继续展示
        if (albumPanel_.isOpen()) {
            HideAlbumPanel();
            return;
        }

        // 6. 预览框可见区域拖动（相册照片同样可拖动视野）
        if (state_.pictureZoomable() && preview_.HitTestViewport(point)) {
            draggingViewport_ = true;
            return;
        }

        // 7. 批注/橡皮：在画面上书写或擦除
        if (state_.tool != core::ToolMode::Select) {
            if (state_.pictureAnnotatable() && annotation_.valid()) {
                float imageX = 0.0f;
                float imageY = 0.0f;
                WindowToImage(x, y, &imageX, &imageY);
                annotation_.PointerDown(imageX, imageY, state_.tool == core::ToolMode::Erase);
                annotating_ = true;
                VB_INFO("%s 开始: 窗口(%d,%d) → 图像(%.1f,%.1f)",
                        state_.tool == core::ToolMode::Erase ? "擦除" : "批注", x, y, imageX,
                        imageY);
            } else {
                VB_WARN("批注被忽略: 画面可交互=%d, 批注层有效=%d",
                        state_.pictureAnnotatable() ? 1 : 0, annotation_.valid() ? 1 : 0);
            }
            return;
        }
    }

    // 选择模式左键或任意模式中键：拖动画面
    draggingView_ = true;
}

void MainWindow::OnButtonUp(int x, int y) {
    if (::GetCapture() == hwnd_) {
        ::ReleaseCapture();
    }

    if (moreSliderDrag_) {
        moreSliderDrag_ = false;
        return;
    }

    // 相册面板：先在抬起位置重新命中，避免误触
    if (albumPanelPressed_) {
        albumPanelPressed_ = false;
        const bool dragged = albumPanel_.WasDragged();
        const AlbumPanelHit pressed = albumPressedHit_;
        albumPressedHit_ = AlbumPanelHit();
        if (albumPanel_.IsDragging()) {
            albumPanel_.EndDrag();
        }
        albumPanelDirty_ = true;
        if (pressed.kind == AlbumPanelHit::Kind::Thumbnail && !dragged) {
            HandleAlbumPanelHit(pressed);
        } else if (pressed.kind != AlbumPanelHit::Kind::None &&
                   pressed.kind != AlbumPanelHit::Kind::Thumbnail) {
            const POINT point = {x, y};
            const AlbumPanelHit released = albumPanel_.HitTest(point);
            if (released.kind == pressed.kind && released.index == pressed.index) {
                HandleAlbumPanelHit(pressed);
            }
        }
        return;
    }

    if (annotating_) {
        annotation_.PointerUp();
        annotating_ = false;
        VB_INFO("笔画结束: 层版本=%llu, 是否为空=%d",
                static_cast<unsigned long long>(annotation_.version()),
                annotation_.empty() ? 1 : 0);
        return;
    }

    const int pressed = pressedIndex_;
    pressedIndex_ = -1;
    if (pressed >= 0) {
        toolbarDirty_ = true;
        const POINT point = {x, y};
        if (toolbar_.HitTest(point) == pressed) {
            HandleToolButton(toolbar_.idAt(pressed));
        }
    }
    draggingView_ = false;
    draggingViewport_ = false;
}

void MainWindow::OnMouseWheel(int delta, int x, int y) {
    // 相册面板内滚轮按横向滚动列表
    const POINT point = {x, y};
    if (albumPanel_.isOpen() && albumPanel_.ContainsPoint(point)) {
        constexpr int kAlbumWheelStep = 140;
        albumPanel_.ScrollBy(-delta / WHEEL_DELTA * static_cast<int>(kAlbumWheelStep * uiScale_));
        albumPanelDirty_ = true;
        return;
    }
    // 相册照片同样支持缩放/拖动（仅不支持批注与拍照）
    if (!state_.pictureZoomable()) {
        ShowToast(L"无摄像头可用，无法缩放画面");
        return;
    }
    ApplyZoomAt(x, y, std::pow(1.1f, static_cast<float>(delta) / 120.0f));
}

void MainWindow::ApplyZoomAt(int x, int y, float factor) {
    if (factor <= 0.0f) {
        return;
    }
    const float oldZoom = state_.zoom;
    const float newZoom = std::max(0.2f, std::min(10.0f, oldZoom * factor));
    if (newZoom == oldZoom) {
        return;
    }
    state_.zoom = newZoom;
    const float k = newZoom / oldZoom;

    // 以锚点为不动点缩放（鼠标滚轮锚点为鼠标位置，双指捏合锚点为两指中心）
    const float relativeX = static_cast<float>(x) - clientWidth_ * 0.5f;
    const float relativeY = static_cast<float>(y) - clientHeight_ * 0.5f;
    state_.offsetX = relativeX - (relativeX - state_.offsetX) * k;
    state_.offsetY = relativeY - (relativeY - state_.offsetY) * k;
    ClampOffsets();
}

void MainWindow::DispatchPointerDown(int x, int y, bool middle) {
    if (settingsDialog_.isOpen()) {
        if (!middle) {
            const POINT point = {x, y};
            settingsDialog_.OnMouseDown(point);
        }
        return;
    }
    OnButtonDown(x, y, middle);
}

void MainWindow::DispatchPointerMove(int x, int y) {
    if (settingsDialog_.isOpen()) {
        const POINT point = {x, y};
        settingsDialog_.OnMouseMove(point);
        return;
    }
    OnMouseMove(x, y);
}

void MainWindow::DispatchPointerUp(int x, int y) {
    if (settingsDialog_.isOpen()) {
        const POINT point = {x, y};
        settingsDialog_.OnMouseUp(point);
        if (!settingsDialog_.isOpen()) {
            FinishSettingsDialog();
        }
        return;
    }
    OnButtonUp(x, y);
}

bool MainWindow::HandlePointerMessage(UINT message, WPARAM wParam) {
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (pointerId == 0) {
        return false;
    }
    POINTER_INFO info = {};
    if (!::GetPointerInfo(pointerId, &info)) {
        return false;
    }
    if (info.pointerType != PT_TOUCH) {
        // 笔/鼠标指针交默认处理（仍需系统合成为鼠标消息才能正常使用）
        if (message == WM_POINTERDOWN) {
            VB_INFO("非触摸指针被忽略: id=%u, type=%d", pointerId,
                    static_cast<int>(info.pointerType));
        }
        return false;
    }
    POINT point = info.ptPixelLocation; // 屏幕物理像素
    if (!::ScreenToClient(hwnd_, &point)) {
        return false;
    }

    const bool removed = message == WM_POINTERUP || message == WM_POINTERLEAVE;
    bool changed = false;
    auto it = std::find_if(touchContacts_.begin(), touchContacts_.end(),
                           [pointerId](const TouchContact& contact) {
                               return contact.id == pointerId;
                           });
    if (removed) {
        if (it != touchContacts_.end()) {
            touchContacts_.erase(it);
            changed = true;
            VB_INFO("触摸抬起: id=%u, 剩余 %zu 指", pointerId, touchContacts_.size());
        }
    } else if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE) {
        if (it != touchContacts_.end()) {
            if (it->pos.x != point.x || it->pos.y != point.y) {
                it->pos = point;
                changed = true;
            }
        } else {
            TouchContact contact;
            contact.id = pointerId;
            contact.pos = point;
            touchContacts_.push_back(contact);
            changed = true;
            VB_INFO("触摸按下: id=%u, 当前 %zu 指", pointerId, touchContacts_.size());
        }
    }
    if (changed) {
        UpdateTouchGesture();
    }
    // 触摸指针一律消费，确保系统不再合成触摸对应的鼠标消息
    return true;
}

float MainWindow::TouchDistance() const {
    if (touchContacts_.size() < 2) {
        return 0.0f;
    }
    const float dx = static_cast<float>(touchContacts_[0].pos.x - touchContacts_[1].pos.x);
    const float dy = static_cast<float>(touchContacts_[0].pos.y - touchContacts_[1].pos.y);
    return std::sqrt(dx * dx + dy * dy);
}

POINT MainWindow::TouchCentroid() const {
    POINT center = {0, 0};
    if (touchContacts_.empty()) {
        return center;
    }
    long long sumX = 0;
    long long sumY = 0;
    for (const TouchContact& contact : touchContacts_) {
        sumX += contact.pos.x;
        sumY += contact.pos.y;
    }
    const long long count = static_cast<long long>(touchContacts_.size());
    center.x = static_cast<LONG>(sumX / count);
    center.y = static_cast<LONG>(sumY / count);
    return center;
}

void MainWindow::UpdateTouchGesture() {
    const size_t count = touchContacts_.size();
    if (count == 0) {
        // 全部抬起：收尾单指动作或结束多指手势
        if (touchMultiActive_) {
            // 诊断：本次手势是否真的执行了平移
            VB_INFO("触摸手势结束: 峰值 %d 指, 平移 %.0f,%.0f px（%d 次更新）",
                    touchGestureMaxCount_, touchPanX_, touchPanY_, touchPanUpdates_);
            touchMultiActive_ = false;
        } else if (touchSingleActive_) {
            touchSingleActive_ = false;
            DispatchPointerUp(touchSinglePos_.x, touchSinglePos_.y);
        }
        touchModeCount_ = 0;
        touchGestureMaxCount_ = 0;
        touchLastDistance_ = 0.0f;
        return;
    }

    if (count == 1) {
        touchSinglePos_ = touchContacts_[0].pos;
        if (touchMultiActive_) {
            // 多指手势收尾阶段保留最后一指不派发，避免误触画面
            return;
        }
        touchModeCount_ = 1;
        if (touchSingleActive_) {
            DispatchPointerMove(touchSinglePos_.x, touchSinglePos_.y);
        } else {
            // 单指等同鼠标左键：选择模式拖动、批注/橡皮书写、点击功能栏与面板
            touchSingleActive_ = true;
            DispatchPointerDown(touchSinglePos_.x, touchSinglePos_.y, false);
        }
        return;
    }

    // 两指及以上：进入手势（先结束正在进行中的单指动作）
    if (!touchMultiActive_) {
        if (touchSingleActive_) {
            touchSingleActive_ = false;
            DispatchPointerUp(touchSinglePos_.x, touchSinglePos_.y);
        }
        touchMultiActive_ = true;
        touchModeCount_ = static_cast<int>(count);
        touchGestureMaxCount_ = static_cast<int>(count);
        touchPanUpdates_ = 0;
        touchPanX_ = 0.0f;
        touchPanY_ = 0.0f;
        touchLastDistance_ = TouchDistance();
        touchLastCentroid_ = TouchCentroid();
        VB_INFO("触摸手势开始：%zu 指", count);
        return;
    }

    if (static_cast<int>(count) != touchModeCount_) {
        // 手指数变化（如抬起一指或接触抖动）：只重置基准，避免画面跳变
        touchModeCount_ = static_cast<int>(count);
        if (touchModeCount_ > touchGestureMaxCount_) {
            touchGestureMaxCount_ = touchModeCount_;
            if (touchGestureMaxCount_ >= 3) {
                VB_INFO("触摸手势切换为多指拖动（%d 指）", touchGestureMaxCount_);
            }
        }
        touchLastDistance_ = TouchDistance();
        touchLastCentroid_ = TouchCentroid();
        VB_INFO("触摸手指数变化: %zu 指（峰值 %d）", count, touchGestureMaxCount_);
        return;
    }

    // 设置面板打开或画面不可缩放时不响应画面手势
    if (settingsDialog_.isOpen() || !state_.pictureZoomable()) {
        return;
    }

    if (touchGestureMaxCount_ >= 3) {
        // 三指及以上：平移画面（本次手势已确认为拖动，抬起一指后仍继续平移）
        const POINT centroid = TouchCentroid();
        const int deltaX = centroid.x - touchLastCentroid_.x;
        const int deltaY = centroid.y - touchLastCentroid_.y;
        if (deltaX != 0 || deltaY != 0) {
            state_.offsetX += static_cast<float>(deltaX);
            state_.offsetY += static_cast<float>(deltaY);
            ClampOffsets();
            ++touchPanUpdates_;
            touchPanX_ += static_cast<float>(deltaX);
            touchPanY_ += static_cast<float>(deltaY);
        }
        touchLastCentroid_ = centroid;
        touchLastDistance_ = TouchDistance();
        return;
    }

    // 双指捏合：按两指距离变化比例缩放，锚点为两指中心
    const float distance = TouchDistance();
    if (touchLastDistance_ > 1.0f && distance > 1.0f) {
        const float factor = distance / touchLastDistance_;
        if (factor > 0.0f && factor < 100.0f) {
            const POINT centroid = TouchCentroid();
            ApplyZoomAt(centroid.x, centroid.y, factor);
        }
    }
    touchLastDistance_ = distance;
    touchLastCentroid_ = TouchCentroid();
}

void MainWindow::OnKeyDown(UINT key) {
    if (key == VK_ESCAPE) {
        if (deps_.onExitRequested) {
            deps_.onExitRequested();
        }
    }
}

void MainWindow::ClampOffsets() {
    if (pictureWidth_ <= 0 || pictureHeight_ <= 0 || clientWidth_ <= 0 || clientHeight_ <= 0) {
        state_.offsetX = 0.0f;
        state_.offsetY = 0.0f;
        return;
    }
    // 位移上限：画面不小于窗口时可一直拖到“画面边缘越过窗口边缘半个窗口”的位置；
    // 画面小于窗口时也允许拖动，极限为画面边缘与窗口边缘对齐（贴边，画面完整可见）
    const float scale = ComputeScale();
    const bool rotated = (state_.rotationQuarter & 1) != 0;
    const float displayWidth = (rotated ? pictureHeight_ : pictureWidth_) * scale;
    const float displayHeight = (rotated ? pictureWidth_ : pictureHeight_) * scale;
    const float windowWidth = static_cast<float>(clientWidth_);
    const float windowHeight = static_cast<float>(clientHeight_);
    const auto limitOf = [](float display, float window) {
        if (display >= window) {
            return (display - window) * 0.5f + window * 0.5f;
        }
        return (window - display) * 0.5f;
    };
    const float limitX = limitOf(displayWidth, windowWidth);
    const float limitY = limitOf(displayHeight, windowHeight);
    state_.offsetX = std::max(-limitX, std::min(limitX, state_.offsetX));
    state_.offsetY = std::max(-limitY, std::min(limitY, state_.offsetY));
}

void MainWindow::HandleToolButton(ToolButtonId id) {
    // 全屏查看照片时：批注/橡皮/旋转/缩放拖动/设置都可用，仅“锁定”不适用
    if (AlbumPhotoShown()) {
        switch (id) {
        case ToolButtonId::Select:
        case ToolButtonId::Annotate:
        case ToolButtonId::Erase:
        case ToolButtonId::Rotate:
        case ToolButtonId::Shoot: // 照片查看时该按钮是“返回相机”
        case ToolButtonId::Album:
        case ToolButtonId::Settings:
        case ToolButtonId::Minimize:
        case ToolButtonId::Exit:
            break;
        default:
            ShowToast(L"照片查看中不支持该操作");
            return;
        }
    }

    switch (id) {
    case ToolButtonId::Select:
        ApplyToolMode(core::ToolMode::Select);
        break;
    case ToolButtonId::Annotate:
        ApplyToolMode(core::ToolMode::Annotate);
        break;
    case ToolButtonId::Erase:
        ApplyToolMode(core::ToolMode::Erase);
        break;
    case ToolButtonId::Rotate:
        if (!state_.pictureZoomable()) {
            ShowToast(L"无摄像头可用，无法旋转画面");
            break;
        }
        state_.rotationQuarter = (state_.rotationQuarter + 1) % 4;
        // 画面顺时针旋转，同步旋转位移以保持视野位置
        std::swap(state_.offsetX, state_.offsetY);
        state_.offsetX = -state_.offsetX;
        ClampOffsets();
        ShowToast(L"画面已旋转 90°");
        break;
    case ToolButtonId::Lock:
        state_.locked = !state_.locked;
        // 锁定即停止拉流（保留设备句柄，解锁后迅速恢复）
        if (deps_.camera != nullptr) {
            deps_.camera->SetActive(!state_.locked);
        }
        ShowToast(state_.locked ? L"画面已锁定" : L"已恢复实时画面");
        if (!state_.locked && deps_.camera != nullptr && !deps_.camera->IsOpen()) {
            state_.picture = core::PictureSource::None;
        }
        break;
    case ToolButtonId::Shoot:
        if (AlbumPhotoShown()) {
            BackToCamera(); // 相机按钮此时是“返回相机”
        } else {
            CapturePhoto();
        }
        break;
    case ToolButtonId::Album:
        if (albumPanel_.isOpen()) {
            HideAlbumPanel(); // 仅收起面板，不影响正在查看的照片
        } else {
            OpenAlbumPanel();
        }
        break;
    case ToolButtonId::Settings:
        OpenSettingsDialog();
        break;
    case ToolButtonId::Minimize:
        EnterMinimized();
        break;
    case ToolButtonId::Exit:
        if (deps_.onExitRequested) {
            deps_.onExitRequested();
        }
        break;
    default:
        break;
    }
    SyncToolbarState();
}

void MainWindow::ApplyToolMode(core::ToolMode mode) {
    // 相册面板与批注/橡皮互斥，切换工具模式时收起面板
    if (albumPanel_.isOpen()) {
        HideAlbumPanel();
    }
    if (mode != core::ToolMode::Select && !state_.pictureAnnotatable()) {
        ShowToast(L"无摄像头可用，无法批注");
        return;
    }
    if (state_.tool == mode) {
        return;
    }
    state_.tool = mode;
    morePanel_.SetMode(mode);
    // 模式按钮变化后，“更多”标签要重新贴到当前所选模式按钮外侧
    Relayout();
    morePanelDirty_ = true;
    VB_INFO("工具模式切换为 %d", static_cast<int>(mode));
}

void MainWindow::OpenSettingsDialog() {
    if (deps_.configStore == nullptr || settingsOpen_) {
        return;
    }
    // 设置面板与相册面板不能同时展示
    if (albumPanel_.isOpen()) {
        HideAlbumPanel();
    }
    settingsOpen_ = true;
    // 设置期间：采集暂停（节省后台性能），批注/拍照禁用
    state_.view = core::ViewMode::Settings;
    SyncToolbarState();
    if (deps_.camera != nullptr) {
        deps_.camera->SetActive(false);
    }

    // 全屏主窗口会被系统按“全屏优化”直接扫描输出，独立设置窗口会被压在画面之下。
    // 因此设置界面不再创建窗口，而是作为一层浮层随展台画面一起绘制、由主窗口转发输入。
    const std::vector<capture::CameraInfo> devices = capture::EnumerateCameras();
    settingsDialog_.Open(hwnd_, deps_.configStore->Get(), devices, uiScale_, clientWidth_,
                         clientHeight_);
    VB_INFO("设置面板已展开，采集已暂停");
}

void MainWindow::FinishSettingsDialog() {
    if (!settingsOpen_) {
        return;
    }
    const bool accepted = settingsDialog_.accepted();
    const core::AppConfig edited = settingsDialog_.result();
    settingsDialog_.Close();
    settingsOpen_ = false;
    settingsTexture_.Destroy();
    // 回到打开设置前的界面状态（可能仍在全屏查看照片）
    state_.view = AlbumPhotoShown() ? core::ViewMode::Album : core::ViewMode::Live;
    SyncToolbarState();

    if (accepted) {
        if (deps_.onSettingsApplied != nullptr) {
            deps_.onSettingsApplied(edited); // 应用层落盘并按需重开采集设备
        }
        ApplyConfigurationFromStore();
        ShowToast(L"设置已保存");
    } else {
        VB_INFO("设置面板已取消");
    }
    if (deps_.camera != nullptr) {
        // 正在查看照片或画面已锁定时不恢复采集，避免无谓的后台解码
        deps_.camera->SetActive(ShouldCapture());
    }
}

bool MainWindow::ShouldCapture() const {
    // 锁定画面或正在查看照片时停止拉流（保留设备句柄，恢复迅速）
    return !state_.locked && !AlbumPhotoShown();
}

void MainWindow::ApplyConfigurationFromStore() {
    if (deps_.configStore == nullptr) {
        return;
    }
    const core::AppConfig& config = deps_.configStore->Get();
    toolbar_.SetVertical(config.IsToolbarVertical());
    gl_.SetVsync(config.render.vsync);
    Relayout();
    SyncToolbarState();
    morePanelDirty_ = true;
    VB_INFO("设置已应用: 功能栏=%s, 垂直同步=%s, 临时目录=%ls",
            config.IsToolbarVertical() ? "两侧" : "底部", config.render.vsync ? "开" : "关",
            deps_.configStore->PhotoDir().c_str());
}

void MainWindow::HandleMorePanelHit(const MorePanelHit& hit) {
    switch (hit.kind) {
    case MorePanelHit::Kind::Color:
        morePanel_.SelectColor(hit.index);
        SyncAnnotationStyle();
        morePanelDirty_ = true;
        break;
    case MorePanelHit::Kind::PenThickness:
        morePanel_.SetPenThickness(hit.index);
        SyncAnnotationStyle();
        morePanelDirty_ = true;
        break;
    case MorePanelHit::Kind::EraserSize:
        morePanel_.SelectEraserSize(hit.index);
        SyncAnnotationStyle();
        morePanelDirty_ = true;
        break;
    case MorePanelHit::Kind::ClearAll:
        annotation_.Clear();
        SaveAnnotation(); // 笔迹清空后同步删除对应文件
        morePanel_.SetOpen(false);
        morePanelDirty_ = true;
        ApplyToolMode(core::ToolMode::Select);
        SyncToolbarState();
        VB_INFO("已清除全部笔迹并回到选择模式");
        ShowToast(L"笔迹已全部清除");
        break;
    default:
        break;
    }
}

std::wstring MainWindow::PhotoDirectory() const {
    return deps_.configStore != nullptr ? deps_.configStore->PhotoDir() : std::wstring();
}

void MainWindow::OpenAlbumPanel() {
    if (deps_.configStore == nullptr || settingsOpen_ || albumPanel_.isOpen()) {
        return;
    }
    photoLibrary_.SetDirectory(PhotoDirectory());
    photoLibrary_.Refresh();
    RefreshShownIndex();

    albumPanel_.SetHover(AlbumPanelHit());
    albumPanel_.SetPhotoCount(photoLibrary_.size());
    albumPanel_.SetShownIndex(albumShownIndex_);
    albumPanel_.ResetScroll();
    albumPressedHit_ = AlbumPanelHit();
    albumPanelPressed_ = false;
    albumPanel_.SetOpen(true);

    // “更多”面板与相册面板互斥
    morePanel_.SetOpen(false);
    morePanelDirty_ = true;

    Relayout();
    SyncToolbarState();
    albumPanelDirty_ = true;
    ShowToast(photoLibrary_.empty()
                  ? std::wstring(L"相册暂无照片")
                  : FormatW(L"相册：共 %llu 张照片",
                            static_cast<unsigned long long>(photoLibrary_.size())));
    VB_INFO("展开相册面板，共 %zu 张照片", photoLibrary_.size());
}

void MainWindow::HideAlbumPanel() {
    if (!albumPanel_.isOpen()) {
        return;
    }
    // 仅收起面板：画面与采集状态（含正在查看的照片）保持不变
    albumPanel_.SetOpen(false);
    albumPanel_.SetHover(AlbumPanelHit());
    albumPressedHit_ = AlbumPanelHit();
    albumPanelPressed_ = false;
    photoLibrary_.Release(); // 释放缩略图缓存
    albumPanelDirty_ = true;
    SyncToolbarState();
    VB_INFO("相册面板已收起");
}

void MainWindow::BackToCamera() {
    HideAlbumPhoto();  // 取消照片展示并恢复采集
    HideAlbumPanel();  // 收起相册面板
    ShowToast(L"已返回相机");
}

void MainWindow::RefreshShownIndex() {
    albumShownIndex_ = -1;
    if (albumShownPath_.empty()) {
        return;
    }
    for (size_t i = 0; i < photoLibrary_.size(); ++i) {
        if (EqualsIgnoreCase(photoLibrary_.at(i).path, albumShownPath_)) {
            albumShownIndex_ = static_cast<long long>(i);
            return;
        }
    }
}

void MainWindow::RefreshAlbumPanelIfOpen() {
    if (!albumPanel_.isOpen()) {
        return;
    }
    photoLibrary_.Refresh();
    RefreshShownIndex();
    albumPanel_.SetPhotoCount(photoLibrary_.size());
    albumPanel_.SetShownIndex(albumShownIndex_);
    // 照片数量变化会影响面板宽度与滚动范围
    Relayout();
    albumPanelDirty_ = true;
}

void MainWindow::ShowAlbumPhoto(size_t index) {
    if (index >= photoLibrary_.size()) {
        return;
    }
    const std::wstring path = photoLibrary_.at(index).path;
    // 再次点击同一张照片返回实时画面
    if (AlbumPhotoShown() && EqualsIgnoreCase(path, albumShownPath_)) {
        HideAlbumPhoto();
        ShowToast(L"已返回实时画面");
        return;
    }

    if (!albumImage_.LoadFromFile(path)) {
        ShowToast(L"照片加载失败");
        return;
    }
    albumShownPath_ = path;
    albumShownName_ = photoLibrary_.at(index).name;
    albumShownIndex_ = static_cast<long long>(index);
    if (!state_.locked) {
        // 非锁定状态：等新采集帧到达后自然替换照片画面
        currentFrame_.reset();
    }
    // 锁定状态保留锁定帧，返回相机时可直接恢复（无需等待采集）
    picturePixels_ = albumImage_.pixels();
    pictureWidth_ = albumImage_.width();
    pictureHeight_ = albumImage_.height();
    pictureStride_ = albumImage_.stride();
    pictureDirty_ = true;
    state_.picture = core::PictureSource::Album;
    state_.view = core::ViewMode::Album; // 照片查看：采集停、相机按钮变“返回相机”
    // 照片按原始比例完整显示
    state_.zoom = 1.0f;
    state_.offsetX = 0.0f;
    state_.offsetY = 0.0f;
    state_.rotationQuarter = 0;
    // 笔迹按图片单独存取：切到该照片自己的笔迹层（照片同样可以批注/擦除）
    EnsureAnnotationFor(albumShownName_, pictureWidth_, pictureHeight_);
    if (deps_.camera != nullptr) {
        deps_.camera->SetActive(false); // 查看照片时暂停采集
    }
    albumPanel_.SetShownIndex(albumShownIndex_);
    albumPanelDirty_ = true;
    SyncToolbarState();
    ShowToast(L"正在查看照片");
    VB_INFO("展示相册照片: %ls (%dx%d)", path.c_str(), pictureWidth_, pictureHeight_);
}

void MainWindow::HideAlbumPhoto() {
    if (!AlbumPhotoShown()) {
        return;
    }
    albumShownPath_.clear();
    albumShownName_.clear();
    albumShownIndex_ = -1;
    // 照片的笔迹写回文件并释放；新的采集帧到达后恢复实时画面的笔迹
    ReleaseAnnotation();
    if (deps_.camera != nullptr) {
        deps_.camera->SetActive(ShouldCapture()); // 恢复采集（画面被锁定时保持停止）
    }
    RestorePictureFromPhoto();
    // 非锁定状态画面像素保留上一张照片，等新的采集帧到达后自然替换（避免闪黑）；
    // albumImage_ 在 UpdatePicture 拿到新帧后再释放。
    state_.picture = core::PictureSource::Locked;
    state_.view = core::ViewMode::Live;
    albumPanel_.SetShownIndex(-1);
    albumPanelDirty_ = true;
    SyncToolbarState();
    VB_INFO("退出照片查看，恢复实时画面");
}

void MainWindow::RestorePictureFromPhoto() {
    if (!state_.locked || !currentFrame_ || currentFrame_->pixels.empty()) {
        return; // 非锁定状态：等待新的采集帧自然替换照片画面
    }
    // 锁定状态下不会再有新帧：直接回到锁定帧，并恢复实时画面的笔迹层
    picturePixels_ = currentFrame_->pixels.data();
    pictureWidth_ = currentFrame_->width;
    pictureHeight_ = currentFrame_->height;
    pictureStride_ = currentFrame_->stride;
    pictureDirty_ = true;
    albumImage_.Reset();
    EnsureAnnotationFor(CurrentAnnotationKey(), pictureWidth_, pictureHeight_);
}

void MainWindow::HandleAlbumPanelHit(const AlbumPanelHit& hit) {
    switch (hit.kind) {
    case AlbumPanelHit::Kind::Thumbnail:
    case AlbumPanelHit::Kind::Show:
        ShowAlbumPhoto(hit.index);
        break;
    case AlbumPanelHit::Kind::Delete:
        DeleteAlbumPhoto(hit.index);
        break;
    case AlbumPanelHit::Kind::Save:
        SaveAlbumPhoto(hit.index);
        break;
    case AlbumPanelHit::Kind::SaveAll:
        SaveAllAlbumPhotos();
        break;
    case AlbumPanelHit::Kind::Import:
        ImportAlbumPhotos();
        break;
    case AlbumPanelHit::Kind::ComposeAnnotation:
        albumPanel_.SetComposeAnnotation(!albumPanel_.composeAnnotation());
        VB_INFO("导出合成笔迹已%s", albumPanel_.composeAnnotation() ? "开启" : "关闭");
        break;
    default:
        break;
    }
    albumPanelDirty_ = true;
}

void MainWindow::SaveAlbumPhoto(size_t index) {
    if (index >= photoLibrary_.size() || hwnd_ == nullptr) {
        return;
    }
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(dialog.GetAddressOf())))) {
        ShowToast(L"无法打开保存对话框");
        return;
    }
    const std::wstring& name = photoLibrary_.at(index).name;
    dialog->SetTitle(L"保存照片");
    dialog->SetFileName(name.c_str());
    const COMDLG_FILTERSPEC filters[] = {{L"JPEG 图片", L"*.jpg"}};
    dialog->SetFileTypes(1, filters);
    dialog->SetDefaultExtension(L"jpg");
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    if (dialog->Show(hwnd_) != S_OK) {
        return;
    }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.GetAddressOf()))) {
        return;
    }
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || raw == nullptr) {
        return;
    }
    const std::wstring target(raw);
    ::CoTaskMemFree(raw);
    // 当前编辑中的笔迹先落盘，再按“合成笔迹”选择框导出
    SaveAnnotation();
    ShowToast(ExportPhoto(index, target, albumPanel_.composeAnnotation()) ? L"照片已保存"
                                                                         : L"照片保存失败");
}

bool MainWindow::ExportPhoto(size_t index, const std::wstring& target, bool withAnnotation) {
    if (index >= photoLibrary_.size() || target.empty()) {
        return false;
    }
    const album::PhotoEntry& entry = photoLibrary_.at(index);
    const std::wstring annotationPath = AnnotationPath(entry.name);
    if (!withAnnotation || !paths::FileExists(annotationPath)) {
        return photoLibrary_.Export(index, target);
    }
    img::Image photo;
    if (!photo.LoadFromFile(entry.path)) {
        VB_WARN("导出时解码照片失败，改为直接复制: %ls", entry.path.c_str());
        return photoLibrary_.Export(index, target);
    }
    const size_t size =
        static_cast<size_t>(photo.stride()) * static_cast<size_t>(photo.height());
    std::vector<uint8_t> composed(photo.pixels(), photo.pixels() + size);
    if (!annotation::CompositeFileInto(annotationPath, composed.data(), photo.width(),
                                       photo.height(), photo.stride())) {
        return photoLibrary_.Export(index, target);
    }
    if (!img::SaveJpeg(target, composed.data(), photo.width(), photo.height(), photo.stride(),
                       kPhotoJpegQuality)) {
        VB_ERROR("导出带笔迹的照片失败: %ls", target.c_str());
        return false;
    }
    VB_INFO("已导出带笔迹的照片: %ls → %ls", entry.path.c_str(), target.c_str());
    return true;
}

size_t MainWindow::ExportAlbumPhotosTo(const std::wstring& folder, bool withAnnotation) {
    size_t saved = 0;
    for (size_t i = 0; i < photoLibrary_.size(); ++i) {
        const std::wstring target =
            UniqueTargetPath(paths::JoinPath(folder, photoLibrary_.at(i).name));
        if (ExportPhoto(i, target, withAnnotation)) {
            ++saved;
        }
    }
    VB_INFO("批量保存照片: %zu/%zu → %ls（%s笔迹）", saved, photoLibrary_.size(), folder.c_str(),
            withAnnotation ? "含" : "不含");
    return saved;
}

size_t MainWindow::ImportPhotoFilesFrom(const std::vector<std::wstring>& files) {
    const std::wstring directory = PhotoDirectory();
    if (directory.empty() || !paths::EnsureDirectory(directory)) {
        VB_ERROR("相册目录不可用，无法导入照片");
        return 0;
    }
    size_t imported = 0;
    for (const std::wstring& source : files) {
        if (ImportPhotoFile(source, directory)) {
            ++imported;
        }
    }
    if (imported > 0) {
        // 相册面板展开时立即刷新（含照片数量为 0 时导入的首批照片）
        photoLibrary_.SetDirectory(directory);
        RefreshAlbumPanelIfOpen();
    }
    return imported;
}

void MainWindow::SaveAllAlbumPhotos() {
    if (hwnd_ == nullptr || photoLibrary_.empty()) {
        ShowToast(L"相册暂无照片");
        return;
    }
    std::wstring folder;
    if (!PickFolderDialog(hwnd_, paths::DesktopDir(), folder)) {
        return; // 用户取消
    }
    // 当前编辑中的笔迹先落盘，保证导出内容与界面一致
    SaveAnnotation();
    const bool withAnnotation = albumPanel_.composeAnnotation();
    const size_t total = photoLibrary_.size();
    const size_t saved = ExportAlbumPhotosTo(folder, withAnnotation);
    ShowToast(saved == total
                  ? FormatW(L"已保存 %llu 张照片", static_cast<unsigned long long>(saved))
                  : FormatW(L"已保存 %llu/%llu 张照片",
                            static_cast<unsigned long long>(saved),
                            static_cast<unsigned long long>(total)));
}

void MainWindow::ImportAlbumPhotos() {
    if (hwnd_ == nullptr) {
        return;
    }
    std::vector<std::wstring> files;
    if (!PickImageDialog(hwnd_, paths::DesktopDir(), true, files)) {
        return; // 用户取消
    }
    const size_t total = files.size();
    const size_t imported = ImportPhotoFilesFrom(files);
    if (imported == 0) {
        ShowToast(L"照片导入失败");
        return;
    }
    ShowToast(imported == total
                  ? FormatW(L"已导入 %llu 张照片", static_cast<unsigned long long>(imported))
                  : FormatW(L"已导入 %llu/%llu 张照片",
                            static_cast<unsigned long long>(imported),
                            static_cast<unsigned long long>(total)));
}

void MainWindow::DeleteAlbumPhoto(size_t index) {
    if (index >= photoLibrary_.size()) {
        return;
    }
    const std::wstring removedName = photoLibrary_.at(index).name;
    const bool deleteShown =
        AlbumPhotoShown() && EqualsIgnoreCase(photoLibrary_.at(index).path, albumShownPath_);
    if (!photoLibrary_.Remove(index)) {
        ShowToast(L"照片删除失败");
        return;
    }
    // 该照片的笔迹文件一并删除
    annotation::RemoveFile(AnnotationPath(removedName));
    if (deleteShown) {
        // 正在查看的照片被删除：丢弃内存中的笔迹（文件已删）并立即回到实时画面
        annotation_.Reset(0, 0);
        annotationTexture_.Destroy();
        annotationKey_.clear();
        annotating_ = false;
        albumShownName_.clear();
        albumShownPath_.clear();
        albumShownIndex_ = -1;
        albumImage_.Reset();
        state_.view = core::ViewMode::Live;
        if (state_.locked && currentFrame_ && !currentFrame_->pixels.empty()) {
            RestorePictureFromPhoto(); // 锁定状态回到锁定帧与实时笔迹层
            state_.picture = core::PictureSource::Locked;
        } else {
            picturePixels_ = nullptr;
            pictureWidth_ = 0;
            pictureHeight_ = 0;
            pictureStride_ = 0;
            pictureDirty_ = false;
            state_.picture = core::PictureSource::None;
        }
        if (deps_.camera != nullptr) {
            deps_.camera->SetActive(ShouldCapture()); // 画面被锁定时保持停止采集
        }
        VB_INFO("正在查看的照片已删除，返回实时画面");
    }
    RefreshShownIndex();
    albumPanel_.SetPhotoCount(photoLibrary_.size());
    albumPanel_.SetShownIndex(albumShownIndex_);
    albumPanel_.SetHover(AlbumPanelHit());
    albumPanelDirty_ = true;
    SyncToolbarState();
    ShowToast(L"照片已删除");
}

void MainWindow::CapturePhoto() {
    if (!state_.pictureShootable()) {
        ShowToast(L"无摄像头可用，无法拍照");
        return;
    }
    if (picturePixels_ == nullptr || pictureWidth_ <= 0 || pictureHeight_ <= 0) {
        ShowToast(L"暂无画面可拍照");
        return;
    }
    const std::wstring photoDir = PhotoDirectory();
    if (photoDir.empty()) {
        ShowToast(L"临时照片目录不可用");
        return;
    }

    SYSTEMTIME time = {};
    ::GetLocalTime(&time);
    const std::wstring photoName = paths::PhotoFileName(time);
    const std::wstring path = paths::JoinPath(photoDir, photoName);

    // 原画面（不含笔迹）交给后台保存线程编码，避免整帧 JPG 编码阻塞界面
    const size_t size =
        static_cast<size_t>(pictureStride_) * static_cast<size_t>(pictureHeight_);
    std::vector<uint8_t> buffer(picturePixels_, picturePixels_ + size);
    saveQueue_.Submit(path, std::move(buffer), pictureWidth_, pictureHeight_, pictureStride_,
                      kPhotoJpegQuality);

    // 笔迹与原画面分离：笔迹单独存为该照片的笔迹文件，
    // 相册中查看时笔迹可继续擦除，导出时再按需合成
    const bool hasAnnotation = annotation_.valid() &&
                               annotation_.width() == pictureWidth_ &&
                               annotation_.height() == pictureHeight_ && !annotation_.empty();
    if (hasAnnotation) {
        annotation::SaveToFile(AnnotationPath(photoName), annotation_);
    }
    VB_INFO("拍照已提交保存%s: %ls (%dx%d)", hasAnnotation ? "（笔迹单独存放）" : "",
            path.c_str(), pictureWidth_, pictureHeight_);
    ShowToast(L"照片已保存");
    // 相册面板展开时，保存线程完成后由 OnTimer 刷新，届时新照片才会落盘
}

void MainWindow::EnterMinimized() {
    if (minimized_) {
        return;
    }
    minimized_ = true;
    // 暂停拉流以节省后台性能，保留设备句柄
    if (deps_.camera != nullptr) {
        deps_.camera->SetActive(false);
    }
    ::ShowWindow(hwnd_, SW_MINIMIZE);
    VB_INFO("展台已最小化，采集与渲染暂停");
}

void MainWindow::LeaveMinimized() {
    if (!minimized_) {
        return;
    }
    minimized_ = false;
    // 正在查看相册照片或画面锁定时不恢复采集，避免无谓的后台解码
    if (deps_.camera != nullptr) {
        deps_.camera->SetActive(ShouldCapture());
    }
    VB_INFO("展台已恢复，采集与渲染继续");
}

void MainWindow::ShowToast(const std::wstring& text) {
    if (text.empty()) {
        return;
    }
    toastText_ = text;
    toastUntil_ = ::GetTickCount64() + kToastDurationMs;
    BuildToastCanvas();
}

void MainWindow::BuildToastCanvas() {
    const int fontPixels = static_cast<int>(20.0f * uiScale_);
    const int paddingX = static_cast<int>(22.0f * uiScale_);
    const int paddingY = static_cast<int>(12.0f * uiScale_);
    const SIZE size = toastCanvas_.MeasureText(toastText_, fontPixels);
    const int width = size.cx + paddingX * 2;
    const int height = size.cy + paddingY * 2;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (!toastCanvas_.Resize(width, height)) {
        return;
    }
    toastCanvas_.Clear();
    const RECT box = {0, 0, width, height};
    toastCanvas_.FillRoundRect(box, height / 2, RGB(18, 18, 18), 205);
    toastCanvas_.DrawText(toastText_, box, fontPixels, RGB(255, 255, 255), 255);

    if (toastTexture_.Upload(toastCanvas_.pixels(), width, height, toastCanvas_.stride())) {
        const int left = (clientWidth_ - width) / 2;
        const int top = static_cast<int>(44.0f * uiScale_);
        toastRect_ = {left, top, left + width, top + height};
    }
}

} // namespace ui
} // namespace vb
