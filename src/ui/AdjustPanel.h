#pragma once

#include <windows.h>

#include "ui/OverlayCanvas.h"
#include "ui/Resources.h"

namespace vb {
namespace ui {

struct AdjustPanelHit {
    enum class Kind { None, Lock, Rotate, Brightness };
    Kind kind = Kind::None;
};

// 「画面调节」浮层：由功能栏的「画面调节」按钮展开，内含锁定开关、旋转按钮与亮度滑块。
// 面板贴在按钮外侧：功能栏在底部时位于按钮上方，功能栏在右侧时位于功能栏左侧。
class AdjustPanel {
public:
    void Init(Resources* resources);
    void SetScale(float uiScale);
    // toolbarBounds：功能栏矩形；anchorButton：画面调节按钮矩形；vertical：功能栏是否纵向
    void Layout(const RECT& toolbarBounds, const RECT& anchorButton, bool vertical,
                int windowWidth, int windowHeight);

    void SetOpen(bool open);
    bool isOpen() const { return open_; }

    void SetLocked(bool locked) { locked_ = locked; }
    bool locked() const { return locked_; }

    void SetBrightness(int percent);
    int brightness() const { return brightness_; }
    // 亮度是否由设备端控制（否则为显示端调节）
    void SetDeviceBrightness(bool device) { deviceBrightness_ = device; }
    bool deviceBrightness() const { return deviceBrightness_; }

    RECT bounds() const { return panelRect_; }
    bool ContainsPoint(POINT point) const;

    void SetHover(AdjustPanelHit::Kind kind) { hoverKind_ = kind; }

    AdjustPanelHit HitTest(POINT point) const;
    // 亮度滑块：按窗口坐标换算亮度值，返回是否发生变化
    bool BrightnessSliderFromPoint(POINT point);

    void Render();
    const OverlayCanvas& canvas() const { return canvas_; }

private:
    void UpdateGeometry();
    int SliderValueFromLocalX(int localX) const;

    Resources* resources_ = nullptr;
    float scale_ = 1.0f;
    bool open_ = false;

    // 最近一次 Layout 的输入（缩放变化时据此重算）
    RECT toolbarBounds_ = {0, 0, 0, 0};
    RECT anchorButton_ = {0, 0, 0, 0};
    bool vertical_ = false;
    int windowWidth_ = 0;
    int windowHeight_ = 0;
    bool laidOut_ = false;

    // 画布局部几何与画布左上角在屏幕中的位置
    int localWidth_ = 0;
    int localHeight_ = 0;
    int screenLeft_ = 0;
    int screenTop_ = 0;
    RECT lockRect_ = {0, 0, 0, 0};
    RECT rotateRect_ = {0, 0, 0, 0};
    RECT sliderTrack_ = {0, 0, 0, 0};
    RECT sliderKnob_ = {0, 0, 0, 0};
    RECT valueRect_ = {0, 0, 0, 0};
    RECT panelRect_ = {0, 0, 0, 0};

    bool locked_ = false;
    int brightness_ = 50;
    bool deviceBrightness_ = false;
    AdjustPanelHit::Kind hoverKind_ = AdjustPanelHit::Kind::None;

    OverlayCanvas canvas_;
};

} // namespace ui
} // namespace vb
