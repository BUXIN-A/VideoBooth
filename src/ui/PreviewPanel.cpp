#include "ui/PreviewPanel.h"

#include "util/Log.h"

#include <algorithm>
#include <cmath>

namespace vb {
namespace ui {
namespace {

constexpr COLORREF kPanelBorderColor = RGB(122, 122, 122);
constexpr COLORREF kPanelBackColor = RGB(52, 52, 52);
constexpr COLORREF kViewportColor = RGB(0, 200, 224);
constexpr COLORREF kLetterboxColor = RGB(24, 24, 24);

float ClampFloat(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

bool SameView(const PreviewView& a, const PreviewView& b) {
    return a.pixels == b.pixels && a.imageWidth == b.imageWidth &&
           a.imageHeight == b.imageHeight && a.stride == b.stride &&
           a.rotationQuarter == b.rotationQuarter && a.windowWidth == b.windowWidth &&
           a.windowHeight == b.windowHeight && a.scale == b.scale &&
           a.offsetX == b.offsetX && a.offsetY == b.offsetY && a.interactive == b.interactive &&
           a.overlayPixels == b.overlayPixels && a.overlayWidth == b.overlayWidth &&
           a.overlayHeight == b.overlayHeight && a.overlayStride == b.overlayStride &&
           a.overlayVersion == b.overlayVersion;
}

} // namespace

void PreviewPanel::SetScale(float uiScale) {
    scale_ = std::max(0.5f, uiScale);
}

void PreviewPanel::Layout(int windowWidth, int windowHeight, const RECT& toolbarBounds) {
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    toolbarBounds_ = toolbarBounds;

    const int margin = static_cast<int>(18.0f * scale_);
    const int panelWidth = static_cast<int>(300.0f * scale_);
    const int panelHeight = static_cast<int>(210.0f * scale_);

    // 可用区域：窗口内缩边距，并按功能栏位置收缩，避免两者相互遮挡
    int availLeft = margin;
    int availTop = margin;
    int availRight = windowWidth - margin;
    int availBottom = windowHeight - margin;
    const bool toolbarVertical =
        (toolbarBounds.right - toolbarBounds.left) < (toolbarBounds.bottom - toolbarBounds.top);
    if (toolbarVertical) {
        // 纵向功能栏贴在右侧
        if (toolbarBounds.left > windowWidth / 2) {
            availRight = std::min(availRight, static_cast<int>(toolbarBounds.left) - margin);
        } else {
            availLeft = std::max(availLeft, static_cast<int>(toolbarBounds.right) + margin);
        }
    } else {
        availBottom = std::min(availBottom, static_cast<int>(toolbarBounds.top) - margin);
    }

    // 默认贴可用区域左下角，再叠加拖动偏移并收敛到可用区域内
    const int maxLeft = std::max(availLeft, availRight - panelWidth);
    const int maxTop = std::max(availTop, availBottom - panelHeight);
    int left = availLeft + positionOffsetX_;
    int top = availBottom - panelHeight + positionOffsetY_;
    left = std::max(availLeft, std::min(left, maxLeft));
    top = std::max(availTop, std::min(top, maxTop));

    // 偏移回写为收敛后的值，使拖动始终落在可用区域内
    positionOffsetX_ = left - availLeft;
    positionOffsetY_ = top - (availBottom - panelHeight);

    const int previousWidth = bounds_.right - bounds_.left;
    const int previousHeight = bounds_.bottom - bounds_.top;
    bounds_ = {left, top, left + panelWidth, top + panelHeight};
    // 位置变化不影响画布内容，仅尺寸变化时需要重绘
    if (previousWidth != panelWidth || previousHeight != panelHeight) {
        dirty_ = true;
    }
}

bool PreviewPanel::ContainsPoint(POINT point) const {
    if (bounds_.right <= bounds_.left || bounds_.bottom <= bounds_.top) {
        return false;
    }
    return point.x >= bounds_.left && point.x < bounds_.right && point.y >= bounds_.top &&
           point.y < bounds_.bottom;
}

void PreviewPanel::BeginPositionDrag(int x, int y) {
    positionDragging_ = true;
    dragOrigin_.x = x;
    dragOrigin_.y = y;
    dragStartOffsetX_ = positionOffsetX_;
    dragStartOffsetY_ = positionOffsetY_;
}

void PreviewPanel::DragPosition(int x, int y) {
    if (!positionDragging_ || windowWidth_ <= 0 || windowHeight_ <= 0) {
        return;
    }
    positionOffsetX_ = dragStartOffsetX_ + (x - dragOrigin_.x);
    positionOffsetY_ = dragStartOffsetY_ + (y - dragOrigin_.y);
    Layout(windowWidth_, windowHeight_, toolbarBounds_);
}

void PreviewPanel::EndPositionDrag() {
    positionDragging_ = false;
}

void PreviewPanel::SetView(const PreviewView& view) {
    if (!validView_ || !SameView(view_, view)) {
        view_ = view;
        dirty_ = true;
    }
}

bool PreviewPanel::Update() {
    if (!dirty_) {
        return false;
    }
    const int width = bounds_.right - bounds_.left;
    const int height = bounds_.bottom - bounds_.top;
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (!canvas_.Resize(width, height)) {
        return false;
    }
    canvas_.Clear();
    dirty_ = false;
    validView_ = true;

    const int radius = static_cast<int>(12.0f * scale_);
    const RECT panelRect = {0, 0, width, height};
    const int border = std::max(1, static_cast<int>(2.0f * scale_));
    canvas_.FillRoundRect(panelRect, radius, kPanelBorderColor, 235);
    RECT innerPanel = {border, border, width - border, height - border};
    canvas_.FillRoundRect(innerPanel, std::max(0, radius - border), kPanelBackColor, 235);

    if (view_.pixels == nullptr || view_.imageWidth <= 0 || view_.imageHeight <= 0) {
        return true;
    }

    const int rotateQuarter = ((view_.rotationQuarter % 4) + 4) % 4;
    const int rotatedWidth = (rotateQuarter & 1) ? view_.imageHeight : view_.imageWidth;
    const int rotatedHeight = (rotateQuarter & 1) ? view_.imageWidth : view_.imageHeight;

    const int padding = static_cast<int>(8.0f * scale_);
    const RECT content = {border + padding, border + padding, width - border - padding,
                          height - border - padding};
    const int contentWidth = content.right - content.left;
    const int contentHeight = content.bottom - content.top;
    if (contentWidth <= 0 || contentHeight <= 0) {
        return true;
    }

    previewScale_ = std::min(static_cast<float>(contentWidth) / rotatedWidth,
                             static_cast<float>(contentHeight) / rotatedHeight);
    const int scaledWidth = std::max(1, static_cast<int>(rotatedWidth * previewScale_));
    const int scaledHeight = std::max(1, static_cast<int>(rotatedHeight * previewScale_));
    const int imageLeft = content.left + (contentWidth - scaledWidth) / 2;
    const int imageTop = content.top + (contentHeight - scaledHeight) / 2;

    const RECT imageRect = {imageLeft, imageTop, imageLeft + scaledWidth,
                            imageTop + scaledHeight};
    canvas_.FillRect(imageRect, kLetterboxColor, 255);
    canvas_.DrawPixels(view_.pixels, view_.imageWidth, view_.imageHeight, view_.stride, imageRect,
                       rotateQuarter, 255);
    if (view_.overlayPixels != nullptr && view_.overlayWidth == view_.imageWidth &&
        view_.overlayHeight == view_.imageHeight) {
        canvas_.DrawPixels(view_.overlayPixels, view_.overlayWidth, view_.overlayHeight,
                           view_.overlayStride, imageRect, rotateQuarter, 255);
    }

    // 计算当前屏幕在整幅画面中的可见区域
    const float windowScale = view_.scale > 0.0001f ? view_.scale : 1.0f;
    const float visibleWidth = static_cast<float>(view_.windowWidth) / windowScale;
    const float visibleHeight = static_cast<float>(view_.windowHeight) / windowScale;
    const float centerX = rotatedWidth * 0.5f - view_.offsetX / windowScale;
    const float centerY = rotatedHeight * 0.5f - view_.offsetY / windowScale;

    const float left = ClampFloat(centerX - visibleWidth * 0.5f, 0.0f,
                                  static_cast<float>(rotatedWidth));
    const float right = ClampFloat(centerX + visibleWidth * 0.5f, 0.0f,
                                   static_cast<float>(rotatedWidth));
    const float top = ClampFloat(centerY - visibleHeight * 0.5f, 0.0f,
                                 static_cast<float>(rotatedHeight));
    const float bottom = ClampFloat(centerY + visibleHeight * 0.5f, 0.0f,
                                    static_cast<float>(rotatedHeight));

    RECT viewport = {imageLeft + static_cast<int>(left * previewScale_),
                     imageTop + static_cast<int>(top * previewScale_),
                     imageLeft + static_cast<int>(right * previewScale_),
                     imageTop + static_cast<int>(bottom * previewScale_)};
    if (viewport.right - viewport.left < 6) {
        viewport.right = std::min(imageRect.right, viewport.left + 6);
    }
    if (viewport.bottom - viewport.top < 6) {
        viewport.bottom = std::min(imageRect.bottom, viewport.top + 6);
    }
    visibleInPanel_ = viewport;

    const int thickness = std::max(1, static_cast<int>(2.0f * scale_));
    canvas_.DrawDashedRect(viewport, kViewportColor, view_.interactive ? 255 : 140, thickness,
                           std::max(4, static_cast<int>(10.0f * scale_)),
                           std::max(3, static_cast<int>(7.0f * scale_)));
    return true;
}

bool PreviewPanel::HitTestViewport(POINT point) const {
    if (bounds_.right <= bounds_.left) {
        return false;
    }
    const int localX = point.x - bounds_.left;
    const int localY = point.y - bounds_.top;
    const int tolerance = static_cast<int>(6.0f * scale_);
    return localX >= visibleInPanel_.left - tolerance &&
           localX < visibleInPanel_.right + tolerance &&
           localY >= visibleInPanel_.top - tolerance &&
           localY < visibleInPanel_.bottom + tolerance;
}

void PreviewPanel::MapDragToOffsetDelta(float deltaX, float deltaY, float* offsetDeltaX,
                                        float* offsetDeltaY) const {
    const float previewScale = previewScale_ > 0.0001f ? previewScale_ : 1.0f;
    const float windowScale = view_.scale > 0.0001f ? view_.scale : 1.0f;
    const float factor = windowScale / previewScale;
    if (offsetDeltaX != nullptr) {
        *offsetDeltaX = -deltaX * factor;
    }
    if (offsetDeltaY != nullptr) {
        *offsetDeltaY = -deltaY * factor;
    }
}

} // namespace ui
} // namespace vb
