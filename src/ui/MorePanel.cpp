#include "ui/MorePanel.h"

#include "util/Log.h"

#include <algorithm>
#include <string>

namespace vb {
namespace ui {
namespace {

constexpr uint32_t kPalette[MorePanel::kColorCount] = {
    0xFF000000, // 黑
    0xFFFFFFFF, // 白
    0xFFFF3B30, // 红
    0xFFFF9500, // 橙
    0xFFFFCC00, // 黄
    0xFF34C759, // 绿
    0xFF32ADE6, // 青
    0xFF0A84FF, // 蓝
    0xFFAF52DE, // 紫
    0xFFFF2D95, // 粉
};

constexpr float kEraserRadius[MorePanel::kEraserSizeCount] = {12.0f, 20.0f, 34.0f, 56.0f, 84.0f};
// 橡皮大小圆点的显示直径（像素，按 UI 比例缩放）
constexpr int kEraserDotDiameter[MorePanel::kEraserSizeCount] = {12, 18, 26, 36, 50};

constexpr COLORREF kPanelColor = RGB(238, 238, 238);
constexpr COLORREF kTitleColor = RGB(70, 70, 70);
constexpr COLORREF kDotColor = RGB(110, 110, 110);
constexpr COLORREF kRingColor = RGB(45, 127, 249);
constexpr COLORREF kBorderColor = RGB(205, 205, 205);
constexpr COLORREF kTrackColor = RGB(203, 207, 214);
constexpr COLORREF kHandleColor = RGB(255, 255, 255);

COLORREF ToColorRef(uint32_t argb) {
    return RGB((argb >> 16) & 0xFFu, (argb >> 8) & 0xFFu, argb & 0xFFu);
}

RECT SquareRect(int centerX, int centerY, int size) {
    const int half = size / 2;
    return {centerX - half, centerY - half, centerX - half + size, centerY - half + size};
}

} // namespace

uint32_t MorePanel::ColorAt(int index) {
    if (index < 0 || index >= kColorCount) {
        return kPalette[0];
    }
    return kPalette[index];
}

float MorePanel::EraserRadiusAt(int index) {
    if (index < 0 || index >= kEraserSizeCount) {
        return kEraserRadius[2];
    }
    return kEraserRadius[index];
}

bool MorePanel::Init(Resources* resources) {
    resources_ = resources;
    return true;
}

void MorePanel::SetScale(float uiScale) {
    scale_ = std::max(0.5f, uiScale);
    if (laidOut_) {
        Layout(toolbarBounds_, anchorButton_, vertical_, windowWidth_, windowHeight_);
    }
}

void MorePanel::SetMode(core::ToolMode mode) {
    if (mode_ != mode) {
        mode_ = mode;
        open_ = false;
        // 面板高度随模式变化（批注含颜色网格与粗细滑块），需按新模式重算几何
        if (laidOut_) {
            Layout(toolbarBounds_, anchorButton_, vertical_, windowWidth_, windowHeight_);
        }
    }
}

void MorePanel::SelectColor(int index) {
    if (index >= 0 && index < kColorCount) {
        colorIndex_ = index;
    }
}

void MorePanel::SelectEraserSize(int index) {
    if (index >= 0 && index < kEraserSizeCount) {
        eraserSizeIndex_ = index;
    }
}

void MorePanel::SetPenThickness(int thickness) {
    penThickness_ = std::max(kPenMinThickness, std::min(thickness, kPenMaxThickness));
}

int MorePanel::PanelWidth() const {
    return static_cast<int>(242.0f * scale_);
}

int MorePanel::PanelHeight() const {
    if (mode_ == core::ToolMode::Erase) {
        return static_cast<int>(164.0f * scale_);
    }
    return static_cast<int>(228.0f * scale_);
}

int MorePanel::TabLongSide() const {
    return static_cast<int>(54.0f * scale_);
}

int MorePanel::TabShortSide() const {
    return static_cast<int>(18.0f * scale_);
}

void MorePanel::Layout(const RECT& toolbarBounds, const RECT& anchorButton, bool vertical,
                       int windowWidth, int windowHeight) {
    toolbarBounds_ = toolbarBounds;
    anchorButton_ = anchorButton;
    vertical_ = vertical;
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    laidOut_ = true;

    const int panelWidth = PanelWidth();
    const int panelHeight = PanelHeight();
    const int tabLong = TabLongSide();
    const int tabShort = TabShortSide();
    const int margin = static_cast<int>(8.0f * scale_);

    // 面板内容始终水平绘制，只有摆放位置与标签朝向随功能栏方向变化
    panelLocal_ = {0, 0, panelWidth, panelHeight};

    if (!vertical) {
        // 功能栏在底部：面板在上，标签贴在面板下沿并正对所选的模式按钮
        localWidth_ = panelWidth;
        localHeight_ = panelHeight + tabShort;
        tabLocal_ = {panelWidth / 2 - tabLong / 2, panelHeight, panelWidth / 2 + tabLong / 2,
                     localHeight_};

        const int anchorCenterX = static_cast<int>((anchorButton.left + anchorButton.right) / 2);
        const int maxLeft = std::max(margin, windowWidth - panelWidth - margin);
        screenLeft_ = std::max(margin, std::min(anchorCenterX - panelWidth / 2, maxLeft));
        screenTop_ = std::max(margin, static_cast<int>(toolbarBounds.top) - localHeight_);
    } else {
        // 功能栏在两侧：面板在左，标签贴在面板右沿并正对所选的模式按钮
        localWidth_ = panelWidth + tabShort;
        localHeight_ = panelHeight;
        tabLocal_ = {panelWidth, panelHeight / 2 - tabLong / 2, localWidth_,
                     panelHeight / 2 + tabLong / 2};

        const int anchorCenterY = static_cast<int>((anchorButton.top + anchorButton.bottom) / 2);
        const int maxTop = std::max(margin, windowHeight - panelHeight - margin);
        screenTop_ = std::max(margin, std::min(anchorCenterY - panelHeight / 2, maxTop));
        screenLeft_ = std::max(margin, static_cast<int>(toolbarBounds.left) - localWidth_);
    }
}

RECT MorePanel::bounds() const {
    return {screenLeft_, screenTop_, screenLeft_ + localWidth_, screenTop_ + localHeight_};
}

RECT MorePanel::tabBounds() const {
    return {screenLeft_ + tabLocal_.left, screenTop_ + tabLocal_.top, screenLeft_ + tabLocal_.right,
            screenTop_ + tabLocal_.bottom};
}

bool MorePanel::ContainsPoint(POINT point) const {
    if (!open_ || !laidOut_) {
        return false;
    }
    const int localX = point.x - screenLeft_;
    const int localY = point.y - screenTop_;
    const RECT& area = panelLocal_;
    return localX >= area.left && localX < area.right && localY >= area.top &&
           localY < area.bottom;
}

void MorePanel::ComputeContentLayout(ContentLayout& layout) const {
    const float s = scale_;
    const int padding = static_cast<int>(16.0f * s);
    const int titleHeight = static_cast<int>(20.0f * s);
    const int sectionGap = static_cast<int>(12.0f * s);
    const int swatchSize = static_cast<int>(34.0f * s);
    const int swatchGap = static_cast<int>(10.0f * s);
    const int dotGap = static_cast<int>(14.0f * s);
    const int dotRowHeight = static_cast<int>(50.0f * s);
    const int contentWidth = static_cast<int>(210.0f * s);

    if (mode_ == core::ToolMode::Erase) {
        // 橡皮模式：仅“橡皮大小”与“全部清除”，无需为颜色网格预留空间
        layout.colorTitle = {0, 0, 0, 0};
        layout.sizeTitle = {padding, padding, padding + contentWidth, padding + titleHeight};
        layout.sizeValue = {0, 0, 0, 0};
        layout.sliderTrack = {0, 0, 0, 0};
        layout.sliderHandle = {0, 0, 0, 0};

        int cursor = padding;
        const int dotsTop = layout.sizeTitle.bottom + static_cast<int>(8.0f * s);
        for (int i = 0; i < kEraserSizeCount; ++i) {
            const int diameter = static_cast<int>(kEraserDotDiameter[i] * s);
            const int centerX = cursor + diameter / 2;
            const int centerY = dotsTop + dotRowHeight / 2;
            layout.dots[i] = SquareRect(centerX, centerY, diameter);
            cursor += diameter + dotGap;
        }
        const int buttonTop = dotsTop + dotRowHeight + sectionGap;
        layout.clearButton = {padding, buttonTop, padding + contentWidth,
                              buttonTop + static_cast<int>(42.0f * s)};
        return;
    }

    layout.colorTitle = {padding, padding, padding + contentWidth, padding + titleHeight};
    const int gridTop = padding + titleHeight + static_cast<int>(8.0f * s);
    for (int i = 0; i < kColorCount; ++i) {
        const int column = i % 5;
        const int row = i / 5;
        const int left = padding + column * (swatchSize + swatchGap);
        const int top = gridTop + row * (swatchSize + swatchGap);
        layout.swatches[i] = {left, top, left + swatchSize, top + swatchSize};
    }
    const int gridBottom = gridTop + 2 * swatchSize + swatchGap;
    layout.sizeTitle = {padding, gridBottom + sectionGap, padding + contentWidth,
                        gridBottom + sectionGap + titleHeight};

    // 粗细滑块：左侧为当前粗细数值，右侧为轨道
    const int rowTop = layout.sizeTitle.bottom + static_cast<int>(8.0f * s);
    const int rowHeight = static_cast<int>(44.0f * s);
    const int valueWidth = static_cast<int>(42.0f * s);
    const int valueGap = static_cast<int>(10.0f * s);
    layout.sizeValue = {padding, rowTop, padding + valueWidth, rowTop + rowHeight};

    const int trackLeft = padding + valueWidth + valueGap;
    const int trackRight = padding + contentWidth;
    const int trackCenterY = rowTop + rowHeight / 2;
    const int trackHalf = std::max(2, static_cast<int>(3.0f * s));
    layout.sliderTrack = {trackLeft, trackCenterY - trackHalf, trackRight,
                          trackCenterY + trackHalf};

    ComputeSliderHandle(layout);
}

void MorePanel::ComputeSliderHandle(ContentLayout& layout) const {
    const RECT& track = layout.sliderTrack;
    if (track.right <= track.left) {
        layout.sliderHandle = {0, 0, 0, 0};
        return;
    }
    const int centerY = (track.top + track.bottom) / 2;
    const int span = static_cast<int>(track.right - track.left);
    const int range = kPenMaxThickness - kPenMinThickness;
    const int offset =
        range > 0 ? span * (penThickness_ - kPenMinThickness) / range : 0;
    const int centerX = static_cast<int>(track.left) + offset;
    const int radius = static_cast<int>(12.0f * scale_);
    layout.sliderHandle = {centerX - radius, centerY - radius, centerX + radius,
                           centerY + radius};
}

int MorePanel::ThicknessFromLocalX(int localX) const {
    ContentLayout layout;
    ComputeContentLayout(layout);
    const RECT& track = layout.sliderTrack;
    const int span = static_cast<int>(track.right - track.left);
    if (span <= 0) {
        return penThickness_;
    }
    const int clamped = std::max(static_cast<int>(track.left),
                                 std::min(localX, static_cast<int>(track.right)));
    const int range = kPenMaxThickness - kPenMinThickness;
    return kPenMinThickness + (clamped - static_cast<int>(track.left)) * range / span;
}

bool MorePanel::PenSliderFromPoint(POINT point) {
    if (!laidOut_ || !open_ || mode_ == core::ToolMode::Erase) {
        return false;
    }
    const int value = ThicknessFromLocalX(point.x - screenLeft_);
    if (value == penThickness_) {
        return false;
    }
    penThickness_ = value;
    return true;
}

MorePanelHit MorePanel::HitTest(POINT point) const {
    MorePanelHit hit;
    if (!open_ || !laidOut_ || mode_ == core::ToolMode::Select) {
        return hit;
    }
    const POINT local = {point.x - screenLeft_, point.y - screenTop_};

    ContentLayout layout;
    ComputeContentLayout(layout);

    const auto inside = [&local](const RECT& rect) {
        return local.x >= rect.left && local.x < rect.right && local.y >= rect.top &&
               local.y < rect.bottom;
    };

    for (int i = 0; i < kColorCount; ++i) {
        if (inside(layout.swatches[i])) {
            hit.kind = MorePanelHit::Kind::Color;
            hit.index = i;
            return hit;
        }
    }

    if (mode_ == core::ToolMode::Erase) {
        // 圆点命中区域放宽为等宽格子，避免小圆点难点中
        const int cellWidth = static_cast<int>(50.0f * scale_);
        for (int i = 0; i < kEraserSizeCount; ++i) {
            const RECT& dot = layout.dots[i];
            RECT cell = {dot.left - cellWidth / 4, dot.top - cellWidth / 4, dot.right + cellWidth / 4,
                         dot.bottom + cellWidth / 4};
            if (inside(cell)) {
                hit.kind = MorePanelHit::Kind::EraserSize;
                hit.index = i;
                return hit;
            }
        }
        if (inside(layout.clearButton)) {
            hit.kind = MorePanelHit::Kind::ClearAll;
        }
        return hit;
    }

    // 粗细滑块：命中范围在轨道基础上向外放宽，便于拖动
    const int sliderPadding = static_cast<int>(16.0f * scale_);
    RECT sliderArea = layout.sliderTrack;
    ::InflateRect(&sliderArea, sliderPadding, sliderPadding);
    if (inside(sliderArea)) {
        hit.kind = MorePanelHit::Kind::PenThickness;
        hit.index = ThicknessFromLocalX(local.x);
        return hit;
    }
    return hit;
}

void MorePanel::RenderTab(OverlayCanvas& canvas, const RECT& rect, int rotateQuarter) {
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    if (width <= 0 || height <= 0) {
        return;
    }
    const img::Image* icon = resources_ != nullptr ? resources_->Get(L"more.png") : nullptr;
    if (icon == nullptr || !icon->Valid()) {
        return;
    }
    canvas.DrawPixels(icon->pixels(), icon->width(), icon->height(), icon->stride(), rect,
                      rotateQuarter, 255);
}

void MorePanel::RenderPanel(OverlayCanvas& canvas, const ContentLayout& layout) {
    const float s = scale_;
    const int panelHeight = PanelHeight();
    const int panelWidth = PanelWidth();
    const RECT panelRect = {0, 0, panelWidth, panelHeight};
    canvas.FillRoundRect(panelRect, static_cast<int>(16.0f * s), kPanelColor, 238);

    const int titleFont = static_cast<int>(14.0f * s);
    const UINT titleFormat = DT_LEFT | DT_VCENTER | DT_SINGLELINE;

    if (mode_ == core::ToolMode::Erase) {
        canvas.DrawText(L"橡皮大小", layout.sizeTitle, titleFont, kTitleColor, 255, titleFormat);
        for (int i = 0; i < kEraserSizeCount; ++i) {
            const RECT& dot = layout.dots[i];
            if (i == eraserSizeIndex_) {
                RECT ring = dot;
                ::InflateRect(&ring, static_cast<int>(4.0f * s), static_cast<int>(4.0f * s));
                canvas.FillRoundRect(ring, static_cast<int>(8.0f * s), kRingColor, 255);
            }
            canvas.FillRoundRect(dot, static_cast<int>(dot.right - dot.left) / 2, kDotColor, 255);
        }
        canvas.FillRoundRect(layout.clearButton, static_cast<int>(10.0f * s), kRingColor, 235);
        canvas.DrawText(L"全部清除", layout.clearButton, titleFont, RGB(255, 255, 255), 255);
        return;
    }

    canvas.DrawText(L"颜色", layout.colorTitle, titleFont, kTitleColor, 255, titleFormat);
    for (int i = 0; i < kColorCount; ++i) {
        const RECT& swatch = layout.swatches[i];
        RECT border = swatch;
        ::InflateRect(&border, static_cast<int>(2.0f * s), static_cast<int>(2.0f * s));
        canvas.FillRoundRect(border, static_cast<int>(9.0f * s),
                             i == colorIndex_ ? kRingColor : kBorderColor, 255);
        canvas.FillRoundRect(swatch, static_cast<int>(7.0f * s), ToColorRef(kPalette[i]), 255);
    }

    canvas.DrawText(L"粗细", layout.sizeTitle, titleFont, kTitleColor, 255, titleFormat);
    const COLORREF penColor = ToColorRef(kPalette[colorIndex_]);

    // 滑块左侧显示当前粗细（整数）
    canvas.DrawText(std::to_wstring(penThickness_), layout.sizeValue,
                    static_cast<int>(17.0f * s), penColor, 255,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const int trackRadius = std::max(1, static_cast<int>(layout.sliderTrack.bottom -
                                                         layout.sliderTrack.top) /
                                            2);
    canvas.FillRoundRect(layout.sliderTrack, trackRadius, kTrackColor, 255);
    // 已选区间用画笔颜色填充
    RECT filled = layout.sliderTrack;
    filled.right = (layout.sliderHandle.left + layout.sliderHandle.right) / 2;
    if (filled.right > filled.left) {
        canvas.FillRoundRect(filled, trackRadius, penColor, 255);
    }
    // 手柄：画笔色描边 + 白色内芯
    canvas.FillRoundRect(layout.sliderHandle, static_cast<int>(12.0f * s), penColor, 255);
    RECT handleInner = layout.sliderHandle;
    ::InflateRect(&handleInner, -static_cast<int>(3.0f * s), -static_cast<int>(3.0f * s));
    canvas.FillRoundRect(handleInner, static_cast<int>(9.0f * s), kHandleColor, 255);
}

void MorePanel::Render() {
    if (localWidth_ <= 0 || localHeight_ <= 0) {
        return;
    }
    const int canvasWidth = localWidth_;
    const int canvasHeight = localHeight_;
    if (!canvas_.Resize(canvasWidth, canvasHeight)) {
        return;
    }
    canvas_.Clear();

    // 纵向功能栏时标签逆时针转 90°：more.png 的窄边原本朝上（背离功能栏），
    // 旋转后应朝左（同样背离位于右侧的功能栏）
    const int tabRotation = vertical_ ? 3 : 0;

    if (!open_ || mode_ == core::ToolMode::Select) {
        RenderTab(canvas_, tabLocal_, tabRotation);
        return;
    }

    ContentLayout layout;
    ComputeContentLayout(layout);
    RenderPanel(canvas_, layout);

    // 标签贴在面板外沿、面向功能栏
    RenderTab(canvas_, tabLocal_, tabRotation);
}

} // namespace ui
} // namespace vb
