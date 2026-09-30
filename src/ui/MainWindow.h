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
#include "core/UpdateChecker.h"
#include "render/GlContext.h"
#include "render/GlRenderer.h"
#include "ui/AdjustPanel.h"
#include "ui/AlbumPanel.h"
#include "ui/MorePanel.h"
#include "ui/OverlayCanvas.h"
#include "ui/PreviewPanel.h"
#include "ui/Resources.h"
#include "ui/SettingsDialog.h"
#include "ui/Toolbar.h"
#include "util/SaveQueue.h"

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
    // 触摸（WM_POINTER）：单指按鼠标事件派发，双指捏合缩放，三指及以上平移
    bool HandlePointerMessage(UINT message, WPARAM wParam);
    void UpdateTouchGesture();
    float TouchDistance() const;
    POINT TouchCentroid() const;
    // 指针（鼠标/触摸）统一派发：设置面板打开时转交设置面板处理
    void DispatchPointerDown(int x, int y, bool middle);
    void DispatchPointerMove(int x, int y);
    void DispatchPointerUp(int x, int y);
    // 以 (x,y) 为锚点缩放画面（鼠标滚轮与双指捏合共用）
    void ApplyZoomAt(int x, int y, float factor);

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
    // 画面调节：旋转 / 锁定 / 亮度
    void ApplyRotate();
    void ToggleLock();
    void ApplyBrightness(int percent);
    void OpenAdjustPanel();
    void HideAdjustPanel();
    void UploadAdjustPanelTexture();
    void HandleAdjustPanelHit(const AdjustPanelHit& hit);
    // 显示端增强参数（锐化 + 亮度），由配置与画面调节共同决定
    gfx::ImageEffect CurrentImageEffect() const;
    // 字体族、锐化等级变化后重新应用到各界面组件
    void ApplyUiFont();
    // 设置面板：直接在展台窗口内渲染（全屏窗口下独立设置窗口不可见）
    void OpenSettingsDialog();
    void FinishSettingsDialog();
    void UploadSettingsTexture();
    void HandleMorePanelHit(const MorePanelHit& hit);
    // 把「更多」面板当前的画笔颜色/粗细/橡皮大小同步到指定笔迹层
    void SyncAnnotationStyle(annotation::StrokeLayer& layer);
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
    // 当前是否应保持采集（锁定画面或正在查看照片时停止拉流）
    bool ShouldCapture() const;
    // 依据 DPI 与配置的 GUI 大小档位计算界面缩放
    float ComputeUiScale() const;
    // 重新计算界面缩放并应用到各界面组件
    void ApplyUiScale();

    // 相册
    bool AlbumPhotoShown() const { return !albumShownPath_.empty(); }
    void OpenAlbumPanel();
    void HideAlbumPanel();
    void BackToCamera();
    void ShowAlbumPhoto(size_t index);
    void HideAlbumPhoto();
    // 退出照片画面：锁定状态恢复锁定帧与实时笔迹层；非锁定状态保留照片像素等待新帧
    void RestorePictureFromPhoto();
    void RefreshShownIndex();
    void RefreshAlbumPanelIfOpen();
    void HandleAlbumPanelHit(const AlbumPanelHit& hit);
    void SaveAlbumPhoto(size_t index);
    void DeleteAlbumPhoto(size_t index);
    // 导出照片：withAnnotation 为真时把该照片的笔迹合成进 JPG
    bool ExportPhoto(size_t index, const std::wstring& target, bool withAnnotation);
    // 进入多选并保存：点「保存照片」进入勾选，点「确定」后把所选照片导出到指定文件夹
    void SaveAllAlbumPhotos();
    void ConfirmSelectedPhotosSave();
    // 从所选图片文件导入到相册目录
    void ImportAlbumPhotos();
    size_t ImportPhotoFilesFrom(const std::vector<std::wstring>& files);
    void UploadAlbumPanelTexture();

    // 对比教学模式：相册面板多选后把所选照片网格平铺到画面中
    bool ComparisonActive() const { return compareMode_ && !compareItems_.empty(); }
    void EnterCompareMode();
    void ExitCompareMode();
    void SyncCompareSelectionFromPanel();
    void ReleaseCompareItems();
    void UploadCompareTextures();
    void RenderCompareGrid(int windowWidth, int windowHeight);
    // 教学网格：格子的宽高与某格中心（照片始终以所在格中心为基准）
    struct CompareCell {
        float width = 0.0f;
        float height = 0.0f;
        float centerX = 0.0f;
        float centerY = 0.0f;
    };
    CompareCell CompareCellAt(size_t itemIndex, int windowWidth, int windowHeight) const;
    // 教学网格的绘制顺序：按叠放层次 z 升序（越晚绘制越靠上）
    std::vector<size_t> CompareDrawOrder() const;
    // 把某张照片置于最顶层（仅改变绘制层次，不改变所在格子）
    void BringCompareItemToFront(size_t itemIndex);
    // 教学网格：某张照片的绘制矩形（含拖动位移与单独缩放），false 表示无有效纹理
    bool CompareItemRect(size_t itemIndex, int windowWidth, int windowHeight, RECT& dest) const;
    // 命中定位：返回位于该点的最上层照片下标，-1 表示未命中
    int CompareItemAt(POINT point, int windowWidth, int windowHeight) const;
    // 窗口坐标 → 该照片的笔迹层坐标
    void CompareWindowToImage(size_t itemIndex, POINT point, int windowWidth, int windowHeight,
                              float* imageX, float* imageY) const;
    // 限制拖动位移，保证照片至少有一部分留在窗口内（可拖到边缘，也能放大后查看局部）
    void ClampCompareOffset(size_t itemIndex, int windowWidth, int windowHeight);
    // 以 (anchorX, anchorY) 为不动点单独缩放某张照片
    void ZoomCompareItem(size_t itemIndex, int anchorX, int anchorY, float factor);
    void SaveCompareAnnotation(size_t itemIndex) const;
    void SaveCompareAnnotations() const;
    void ClearCompareAnnotations();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    MainWindowDeps deps_;
    core::UiState state_;

    gfx::GlContext gl_;
    gfx::GlRenderer renderer_;
    Toolbar toolbar_;
    PreviewPanel preview_;
    MorePanel morePanel_;
    AdjustPanel adjustPanel_;
    AlbumPanel albumPanel_;
    SettingsDialog settingsDialog_;

    gfx::Texture pictureTexture_;
    gfx::Texture annotationTexture_;
    gfx::Texture toolbarTexture_;
    gfx::Texture previewTexture_;
    gfx::Texture toastTexture_;
    gfx::Texture morePanelTexture_;
    gfx::Texture adjustPanelTexture_;
    gfx::Texture albumPanelTexture_;
    gfx::Texture settingsTexture_; // 设置面板
    gfx::Texture eraserTexture_; // 橡皮擦除范围指示
    OverlayCanvas toastCanvas_;

    annotation::StrokeLayer annotation_;
    bool annotating_ = false;
    bool moreSliderDrag_ = false;   // 正在拖动“更多”面板的粗细滑块
    bool adjustSliderDrag_ = false; // 正在拖动「画面调节」的亮度滑块
    // 亮度：优先设备端，设备不支持时改为显示端调节
    int brightnessPercent_ = 50;
    bool displayBrightness_ = false;
    float sharpenStrength_ = 0.0f; // 由配置的锐化等级换算出的强度
    bool adjustPanelDirty_ = true;
    AdjustPanelHit::Kind adjustPanelHover_ = AdjustPanelHit::Kind::None;

    // 对比教学模式：多选的图片在画面中网格平铺，可单独拖动并各自批注
    struct CompareItem {
        size_t index = 0;
        std::wstring name;      // 照片文件名（笔迹文件以其命名）
        img::Image image;
        gfx::Texture texture;
        bool dirty = true;
        // 拖动位移与单独缩放（仅本次教学有效，不写盘）
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float zoom = 1.0f;
        int z = 0; // 叠放层次：越大越靠上（双击置顶）
        // 每张照片各自的笔迹层（与全屏查看共用 <照片名>.ann.png）
        annotation::StrokeLayer annotation;
        gfx::Texture annotationTexture;
    };
    bool compareMode_ = false;
    std::vector<CompareItem> compareItems_;
    int compareDragIndex_ = -1;       // 选择模式下正在拖动的照片下标
    POINT compareDragLast_ = {0, 0};  // 拖动上一位置（窗口坐标）
    int compareAnnotateIndex_ = -1;   // 批注/橡皮时命中的照片下标
    int compareTopZ_ = 0;             // 叠放层次计数（越大越靠上）
    // 双击判定（鼠标与触摸都经 OnButtonDown，故统一在此判定）
    ULONGLONG compareLastClickTick_ = 0;
    POINT compareLastClickPos_ = {0, 0};
    int compareLastClickIndex_ = -1;

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
    float dpiScale_ = 1.0f; // 系统 DPI 对应的基础缩放
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

    // 触摸手势状态：触摸点集合 + 单指/多指派发与缩放基准
    struct TouchContact {
        UINT32 id = 0; // WM_POINTER 的 pointerId
        POINT pos = {0, 0};
    };
    std::vector<TouchContact> touchContacts_;
    bool touchSingleActive_ = false;   // 单指已按鼠标事件派发
    bool touchMultiActive_ = false;    // 处于多指手势（锁存到全部抬起）
    int touchModeCount_ = 0;           // 上一次手势使用的手指数
    int touchGestureMaxCount_ = 0;     // 本次手势出现过的最大手指数（用于锁定缩放/平移模式）
    float touchLastDistance_ = 0.0f;   // 双指距离基准
    POINT touchLastCentroid_ = {0, 0}; // 手势中心基准
    POINT touchSinglePos_ = {0, 0};

    // 后台 JPG 保存线程（拍照等整帧编码不再阻塞界面）
    img::JpegSaveQueue saveQueue_;
    unsigned long long lastSaveCompleted_ = 0;

    // 检查更新（后台请求 GitHub 最新 release，结果由设置面板「关于」页展示）
    core::UpdateChecker updateChecker_;

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
    ULONGLONG flashStartMs_ = 0; // 拍照白闪的起始时刻，0 表示未触发
};

} // namespace ui
} // namespace vb
