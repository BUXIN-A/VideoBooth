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

// 左下角预览框：显示画面全景，蓝色虚线框表示当前屏幕可见区域，可拖动
class PreviewPanel {
public:
    void SetScale(float uiScale);
    void Layout(int windowHeight);
    const RECT& bounds() const { return bounds_; }

    void SetView(const PreviewView& view);
    bool Update();

    const OverlayCanvas& canvas() const { return canvas_; }

    // 命中蓝色虚线框（用于拖动画面）
    bool HitTestViewport(POINT point) const;
    // 把预览框中的拖动位移换算为窗口偏移增量
    void MapDragToOffsetDelta(float deltaX, float deltaY, float* offsetDeltaX,
                              float* offsetDeltaY) const;

private:
    float scale_ = 1.0f;
    RECT bounds_ = {};
    float previewScale_ = 1.0f;

    PreviewView view_;
    bool dirty_ = true;
    bool validView_ = false;

    // 可见区域（预览框画布坐标）
    RECT visibleInPanel_ = {0, 0, 0, 0};

    OverlayCanvas canvas_;
};

} // namespace ui
} // namespace vb
