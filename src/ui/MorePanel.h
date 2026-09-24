#pragma once

#include <windows.h>

#include <cstdint>

#include "core/AppState.h"
#include "ui/OverlayCanvas.h"
#include "ui/Resources.h"

namespace vb {
namespace ui {

struct MorePanelHit {
    enum class Kind { None, Color, PenThickness, EraserSize, ClearAll };
    Kind kind = Kind::None;
    // Color / EraserSize 为列表下标；PenThickness 为滑块换算出的实际粗细像素值
    int index = -1;
};

// “更多”面板：批注模式下设置画笔颜色与粗细（滑块），橡皮模式下设置橡皮大小与全部清除。
// 面板通过“更多”标签（more.png）贴在所选模式按钮外侧：
// 功能栏在底部时标签位于按钮正上方，功能栏在两侧时标签位于按钮左侧。
// 两种情况下面板内容都保持水平绘制，仅面板摆放位置与标签朝向不同。
class MorePanel {
public:
    static constexpr int kColorCount = 10;
    static constexpr int kEraserSizeCount = 5;
    static constexpr int kPenMinThickness = 1;
    static constexpr int kPenMaxThickness = 30;

    static uint32_t ColorAt(int index);
    static float EraserRadiusAt(int index);

    bool Init(Resources* resources);
    void SetScale(float uiScale);
    // toolbarBounds：功能栏矩形；anchorButton：所选模式按钮矩形；vertical：功能栏是否纵向
    void Layout(const RECT& toolbarBounds, const RECT& anchorButton, bool vertical, int windowWidth,
                int windowHeight);

    void SetMode(core::ToolMode mode);
    void SetOpen(bool open) { open_ = open; }
    bool isOpen() const { return open_; }

    // 批注/橡皮模式下才显示“更多”标签
    bool modeVisible() const { return mode_ != core::ToolMode::Select; }

    RECT tabBounds() const; // “更多”标签的屏幕矩形（用于命中判定）
    RECT bounds() const;    // 面板 + 标签的屏幕占用区域
    bool ContainsPoint(POINT point) const;

    MorePanelHit HitTest(POINT point) const;

    int colorIndex() const { return colorIndex_; }
    uint32_t color() const { return ColorAt(colorIndex_); }
    int penThickness() const { return penThickness_; }
    int eraserSizeIndex() const { return eraserSizeIndex_; }
    float eraserRadius() const { return EraserRadiusAt(eraserSizeIndex_); }

    void SelectColor(int index);
    void SelectEraserSize(int index);
    void SetPenThickness(int thickness);
    // 粗细滑块拖动：按窗口坐标换算粗细值，返回是否发生变化
    bool PenSliderFromPoint(POINT point);

    void Render();
    const OverlayCanvas& canvas() const { return canvas_; }

private:
    // 面板内容几何（画布局部坐标，仅展开时有效）
    struct ContentLayout {
        RECT colorTitle = {0, 0, 0, 0};
        RECT swatches[kColorCount] = {};
        RECT sizeTitle = {0, 0, 0, 0};
        RECT sizeValue = {0, 0, 0, 0}; // 粗细数值（滑块左侧）
        RECT sliderTrack = {0, 0, 0, 0};
        RECT sliderHandle = {0, 0, 0, 0};
        RECT dots[kEraserSizeCount] = {};
        RECT clearButton = {0, 0, 0, 0};
    };

    int PanelWidth() const;
    int PanelHeight() const;
    int TabLongSide() const;  // 标签沿功能栏方向的长边
    int TabShortSide() const; // 标签垂直功能栏方向的短边
    void ComputeContentLayout(ContentLayout& layout) const;
    void ComputeSliderHandle(ContentLayout& layout) const;
    int ThicknessFromLocalX(int localX) const;
    void RenderTab(OverlayCanvas& canvas, const RECT& rect, int rotateQuarter);
    void RenderPanel(OverlayCanvas& canvas, const ContentLayout& layout);

    Resources* resources_ = nullptr;
    float scale_ = 1.0f;
    core::ToolMode mode_ = core::ToolMode::Select;
    bool open_ = false;

    // 最近一次 Layout 的输入，模式/缩放变化时据此重算几何
    RECT toolbarBounds_ = {0, 0, 0, 0};
    RECT anchorButton_ = {0, 0, 0, 0};
    bool vertical_ = false;
    int windowWidth_ = 0;
    int windowHeight_ = 0;
    bool laidOut_ = false;

    // 画布局部几何与画布左上角在屏幕中的位置
    int localWidth_ = 0;
    int localHeight_ = 0;
    RECT panelLocal_ = {0, 0, 0, 0};
    RECT tabLocal_ = {0, 0, 0, 0};
    int screenLeft_ = 0;
    int screenTop_ = 0;

    int colorIndex_ = 2; // 默认红色
    int penThickness_ = 5;
    int eraserSizeIndex_ = 2;

    OverlayCanvas canvas_;
};

} // namespace ui
} // namespace vb
