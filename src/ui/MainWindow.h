#pragma once

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "album/PhotoLibrary.h"
#include "annotation/StrokeLayer.h"
#include "capture/Camera.h"
#include "core/AppState.h"
#include "core/Config.h"
#include "render/GlContext.h"
#include "render/GlRenderer.h"
#include "ui/AlbumPanel.h"
#include "ui/MorePanel.h"
#include "ui/OverlayCanvas.h"
#include "ui/PreviewPanel.h"
#include "ui/Resources.h"
#include "ui/SettingsDialog.h"
#include "ui/Toolbar.h"

namespace vb {
namespace ui {

// 主窗口类名（单实例检测时用于查找已运行实例的窗口）
inline constexpr wchar_t kMainWindowClassName[] = L"VideoBoothMainWindow";

struct MainWindowDeps {
    Resources* resources = nullptr;
    capture::CameraCapture* camera = nullptr;
    const img::Image* errorImage = nullptr;
    core::ConfigStore* configStore = nullptr;
    std::function<void()> onExitRequested;
    // 设置保存后回调，由应用层落盘并在需要时重开采集设备
    std::function<void(const core::AppConfig&)> onSettingsApplied;
    std::function<void()> onCameraMaintain; // 定时巡检摄像头（热插拔）
};

// 全屏展台主窗口：OpenGL 画面渲染 + Win32 输入处理
class MainWindow {
public:
    ~MainWindow();

    bool Create(HINSTANCE instance, const MainWindowDeps& deps);
    void Destroy();
    void RunMessageLoop();
    void RequestClose();
    // 设置变更后重新应用界面相关项（功能栏位置、垂直同步、临时目录）
    void ApplyConfigurationFromStore();

    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void OnRender();
    void OnTimer();
    void OnMouseMove(int x, int y);
    void OnMouseLeave();
    void OnButtonDown(int x, int y, bool middle);
    void OnButtonUp(int x, int y);
    void OnMouseWheel(int delta, int x, int y);
    void OnKeyDown(UINT key);

    void Relayout();
    void SyncToolbarState();
    void UpdatePicture();
    void SetErrorPicture();
    void UpdatePreview();
    void UploadOverlayTextures();
    void BuildToastCanvas();
    void ShowToast(const std::wstring& text);
    void HandleToolButton(ToolButtonId id);
    void ApplyToolMode(core::ToolMode mode);
    // 设置面板：直接在展台窗口内渲染（全屏窗口下独立设置窗口不可见）
    void OpenSettingsDialog();
    void FinishSettingsDialog();
    void UploadSettingsTexture();
    void HandleMorePanelHit(const MorePanelHit& hit);
    void SyncAnnotationStyle();
    // 笔迹按图片单独存取：照片用文件名，实时画面用固定标识（均为临时目录中的文件）
    std::wstring CurrentAnnotationKey() const;
    std::wstring AnnotationPath(const std::wstring& key) const;
    void EnsureAnnotationFor(const std::wstring& key, int imageWidth, int imageHeight);
    void SaveAnnotation() const;
    void ReleaseAnnotation();
    void WindowToImage(int windowX, int windowY, float* imageX, float* imageY) const;
    // 橡皮模式下的圆形擦除范围指示
    void UpdateEraserCursor();
    bool EraserCursorVisible() const;
    void CapturePhoto();
    void EnterMinimized();
    void LeaveMinimized();
    void ClampOffsets();
    float ComputeScale() const;
    std::wstring PhotoDirectory() const;

    // 相册
    bool AlbumPhotoShown() const { return !albumShownPath_.empty(); }
    void OpenAlbumPanel();
    void HideAlbumPanel();
    void BackToCamera();
    void ShowAlbumPhoto(size_t index);
    void HideAlbumPhoto();
    void RefreshShownIndex();
    void RefreshAlbumPanelIfOpen();
    void HandleAlbumPanelHit(const AlbumPanelHit& hit);
    void SaveAlbumPhoto(size_t index);
    void DeleteAlbumPhoto(size_t index);
    // 导出照片：withAnnotation 为真时把该照片的笔迹合成进 JPG
    bool ExportPhoto(size_t index, const std::wstring& target, bool withAnnotation);
    // 把相册全部照片复制到所选文件夹
    void SaveAllAlbumPhotos();
    // 从所选图片文件导入到相册目录
    void ImportAlbumPhotos();
    // 核心动作（与文件对话框解耦，便于复用与验证）
    size_t ExportAlbumPhotosTo(const std::wstring& folder, bool withAnnotation);
    size_t ImportPhotoFilesFrom(const std::vector<std::wstring>& files);
    void UploadAlbumPanelTexture();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    MainWindowDeps deps_;
    core::UiState state_;

    gfx::GlContext gl_;
    gfx::GlRenderer renderer_;
    Toolbar toolbar_;
    PreviewPanel preview_;
    MorePanel morePanel_;
    AlbumPanel albumPanel_;
    SettingsDialog settingsDialog_;

    gfx::Texture pictureTexture_;
    gfx::Texture annotationTexture_;
    gfx::Texture toolbarTexture_;
    gfx::Texture previewTexture_;
    gfx::Texture toastTexture_;
    gfx::Texture morePanelTexture_;
    gfx::Texture albumPanelTexture_;
    gfx::Texture settingsTexture_; // 设置面板
    gfx::Texture eraserTexture_; // 橡皮擦除范围圆环
    OverlayCanvas toastCanvas_;

    annotation::StrokeLayer annotation_;
    bool annotating_ = false;
    bool moreSliderDrag_ = false; // 正在拖动“更多”面板的粗细滑块

    // 相册数据与展示状态
    album::PhotoLibrary photoLibrary_;
    img::Image albumImage_;
    std::wstring albumShownPath_;    // 正在全屏展示的照片路径，空表示未展示
    std::wstring albumShownName_;    // 正在展示的照片文件名（笔迹文件以其命名）
    std::wstring annotationKey_;     // 当前笔迹层归属：空 = 实时画面，否则为照片文件名
    long long albumShownIndex_ = -1; // 由 albumShownPath_ 在列表中的位置推导
    bool albumPanelDirty_ = true;
    bool albumPanelPressed_ = false;
    AlbumPanelHit albumPressedHit_;

    capture::FramePtr currentFrame_;
    const uint8_t* picturePixels_ = nullptr;
    int pictureWidth_ = 0;
    int pictureHeight_ = 0;
    int pictureStride_ = 0;
    bool pictureDirty_ = false;
    uint64_t lastFrameIndex_ = 0;

    float uiScale_ = 1.0f;
    int clientWidth_ = 0;
    int clientHeight_ = 0;

    int hoverIndex_ = -1;
    int pressedIndex_ = -1;
    bool draggingView_ = false;
    bool draggingViewport_ = false;
    POINT lastMouse_ = {0, 0};
    POINT cursorPos_ = {0, 0};       // 最近一次鼠标客户区位置
    bool cursorInside_ = false;      // 鼠标是否位于窗口客户区内
    int eraserCursorDiameter_ = 0;   // 已生成的圆环直径（屏幕像素）

    bool toolbarDirty_ = true;
    bool morePanelDirty_ = true;
    bool minimized_ = false;
    bool firstFrameReported_ = false;
    bool settingsOpen_ = false;

    std::wstring toastText_;
    ULONGLONG toastUntil_ = 0;
    RECT toastRect_ = {0, 0, 0, 0};

    ULONGLONG lastPreviewRefresh_ = 0;
    ULONGLONG lastCameraMaintain_ = 0;
    ULONGLONG lastPresentTick_ = 0;
};

} // namespace ui
} // namespace vb
