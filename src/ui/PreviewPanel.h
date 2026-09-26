#pragma once

#include <windows.h>

#include <cstdint>

#include "ui/OverlayCanvas.h"

namespace vb {
namespace ui {

// 主画面视图描述：缩放为「窗口像素 / 旋转后图像像素」
struct PreviewView {
    const uint8_t* pixels = nullptr;
    int imageWidth = 0;
    int imageHeight = 0;
    int stride = 0;
    int rotationQuarter = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    float scale = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool interactive = true;
    // 批注层（可为空），与画面同尺寸、同变换
    const uint8_t* overlayPixels = nullptr;
    int overlayWidth = 0;
    int overlayHeight = 0;
    int overlayStride = 0;
    uint64_t overlayVersion = 0;
};

// 左下角预览框：显示画面全景，蓝色虚线框表示当前屏幕可见区域，可拖动。
// 预览框本身也可按住拖动改变位置（仅本次运行有效），位置限制在避开功能栏的可用区域内。
class PreviewPanel {
public:
    void SetScale(float uiScale);
    // 依据窗口尺寸与功能栏矩形重算预览框位置（避免与功能栏相互遮挡）
    void Layout(int windowWidth, int windowHeight, const RECT& toolbarBounds);
    const RECT& bounds() const { return bounds_; }

    void SetView(const PreviewView& view);
    bool Update();

    const OverlayCanvas& canvas() const { return canvas_; }

    // 命中蓝色虚线框（用于拖动画面）
    bool HitTestViewport(POINT point) const;
    // 命中预览框整体（用于拖动预览框本身）
    bool ContainsPoint(POINT point) const;
    // 把预览框中的拖动位移换算为窗口偏移增量
    void MapDragToOffsetDelta(float deltaX, float deltaY, float* offsetDeltaX,
                              float* offsetDeltaY) const;

    void BeginPositionDrag(int x, int y);
    void DragPosition(int x, int y);
    void EndPositionDrag();
    bool IsPositionDragging() const { return positionDragging_; }

private:
    float scale_ = 1.0f;
    RECT bounds_ = {};
    float previewScale_ = 1.0f;

    PreviewView view_;
    bool dirty_ = true;
    bool validView_ = false;

    // 可见区域（预览框画布坐标）
    RECT visibleInPanel_ = {0, 0, 0, 0};

    // 最近一次布局输入，拖动位置时据此重算
    int windowWidth_ = 0;
    int windowHeight_ = 0;
    RECT toolbarBounds_ = {};
    // 相对默认位置（可用区域左下角）的偏移
    int positionOffsetX_ = 0;
    int positionOffsetY_ = 0;

    bool positionDragging_ = false;
    POINT dragOrigin_ = {0, 0};
    int dragStartOffsetX_ = 0;
    int dragStartOffsetY_ = 0;

    OverlayCanvas canvas_;
};

} // namespace ui
} // namespace vb
