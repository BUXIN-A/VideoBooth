#include "ui/AdjustPanel.h"

#include "util/Strings.h"

#include <algorithm>

namespace vb {
namespace ui {
namespace {

// 逻辑尺寸（按 UI 比例缩放）
constexpr float kPanelWidth = 300.0f;
constexpr float kPadding = 16.0f;
constexpr float kRowHeight = 34.0f;
constexpr float kRowGap = 12.0f;
constexpr float kLabelWidth = 64.0f;
constexpr float kSwitchWidth = 56.0f;
constexpr float kSwitchHeight = 28.0f;
constexpr float kButtonWidth = 128.0f;
constexpr float kButtonHeight = 34.0f;
constexpr float kValueWidth = 74.0f;
constexpr float kRowGapX = 8.0f;
constexpr float kSliderTrackHeight = 6.0f;
constexpr float kSliderKnob = 18.0f;
constexpr float kPanelHeight =
    kPadding * 2 + kRowHeight * 3 + kRowGap * 2;

constexpr COLORREF kPanelColor = RGB(240, 240, 240);
constexpr COLORREF kBorderColor = RGB(172, 175, 180);
constexpr COLORREF kLabelColor = RGB(28, 30, 34);
constexpr COLORREF kValueColor = RGB(58, 64, 74);
constexpr COLORREF kButtonColor = RGB(226, 226, 226);
constexpr COLORREF kButtonHover = RGB(198, 214, 240);
constexpr COLORREF kSwitchOff = RGB(184, 190, 198);
constexpr COLORREF kSwitchKnob = RGB(255, 255, 255);
constexpr COLORREF kAccentColor = RGB(45, 127, 249);
constexpr COLORREF kTrackColor = RGB(176, 182, 192);

bool InsideRect(int x, int y, const RECT& rect) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

void AdjustPanel::Init(Resources* resources) {
    resources_ = resources;
}

void AdjustPanel::SetScale(float uiScale) {
    scale_ = std::max(0.5f, uiScale);
    if (laidOut_) {
        Layout(toolbarBounds_, anchorButton_, vertical_, windowWidth_, windowHeight_);
    }
}

void AdjustPanel::SetOpen(bool open) {
    if (open_ == open) {
        return;
    }
    open_ = open;
    if (!open_) {
        hoverKind_ = AdjustPanelHit::Kind::None;
    }
}

void AdjustPanel::SetBrightness(int percent) {
    brightness_ = std::max(0, std::min(100, percent));
}

void AdjustPanel::UpdateGeometry() {
    const float s = scale_;
    localWidth_ = static_cast<int>(kPanelWidth * s);
    localHeight_ = static_cast<int>(kPanelHeight * s);

    const int padding = static_cast<int>(kPadding * s);
    const int rowHeight = static_cast<int>(kRowHeight * s);
    const int rowGap = static_cast<int>(kRowGap * s);
    const int labelWidth = static_cast<int>(kLabelWidth * s);
    const int controlLeft = padding + labelWidth;
    const int controlRight = localWidth_ - padding;

    const auto rowTop = [&](int index) { return padding + index * (rowHeight + rowGap); };

    const int switchWidth = static_cast<int>(kSwitchWidth * s);
    const int switchHeight = static_cast<int>(kSwitchHeight * s);
    lockRect_ = {controlLeft, rowTop(0) + (rowHeight - switchHeight) / 2, controlLeft + switchWidth,
                 rowTop(0) + (rowHeight - switchHeight) / 2 + switchHeight};

    const int buttonWidth = static_cast<int>(kButtonWidth * s);
    const int buttonHeight = static_cast<int>(kButtonHeight * s);
    rotateRect_ = {controlLeft, rowTop(1) + (rowHeight - buttonHeight) / 2,
                   controlLeft + buttonWidth, rowTop(1) + (rowHeight - buttonHeight) / 2 + buttonHeight};

    const int valueWidth = static_cast<int>(kValueWidth * s);
    const int sliderLeft = controlLeft;
    const int sliderRight = controlRight - valueWidth - static_cast<int>(kRowGapX * s);
    const int knob = static_cast<int>(kSliderKnob * s);
    const int trackTop = rowTop(2) + (rowHeight - static_cast<int>(kSliderTrackHeight * s)) / 2;
    sliderTrack_ = {sliderLeft, trackTop, std::max(sliderLeft + 1, sliderRight),
                    trackTop + static_cast<int>(kSliderTrackHeight * s)};
    valueRect_ = {sliderRight + static_cast<int>(kRowGapX * s), rowTop(2), controlRight,
                  rowTop(2) + rowHeight};

    const int trackWidth = static_cast<int>(sliderTrack_.right - sliderTrack_.left);
    const int knobLeft = sliderTrack_.left + (trackWidth - knob) * brightness_ / 100;
    const int trackCenterY = static_cast<int>(sliderTrack_.top + sliderTrack_.bottom) / 2;
    sliderKnob_ = {knobLeft, trackCenterY - knob / 2, knobLeft + knob,
                   trackCenterY - knob / 2 + knob};
}

void AdjustPanel::Layout(const RECT& toolbarBounds, const RECT& anchorButton, bool vertical,
                         int windowWidth, int windowHeight) {
    toolbarBounds_ = toolbarBounds;
    anchorButton_ = anchorButton;
    vertical_ = vertical;
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    laidOut_ = true;

    UpdateGeometry();

    const int margin = static_cast<int>(8.0f * scale_);
    if (!vertical) {
        // 功能栏在底部：面板贴在功能栏上方，与「画面调节」按钮对齐
        const int anchorCenterX = static_cast<int>((anchorButton.left + anchorButton.right) / 2);
        const int maxLeft = std::max(margin, windowWidth - localWidth_ - margin);
        screenLeft_ = std::max(margin, std::min(anchorCenterX - localWidth_ / 2, maxLeft));
        screenTop_ = std::max(margin, static_cast<int>(toolbarBounds.top) - localHeight_ - margin);
    } else {
        // 功能栏在右侧：面板贴在功能栏左侧，与按钮垂直居中对齐
        const int anchorCenterY = static_cast<int>((anchorButton.top + anchorButton.bottom) / 2);
        const int maxTop = std::max(margin, windowHeight - localHeight_ - margin);
        screenTop_ = std::max(margin, std::min(anchorCenterY - localHeight_ / 2, maxTop));
        screenLeft_ = std::max(margin, static_cast<int>(toolbarBounds.left) - localWidth_ - margin);
    }
    panelRect_ = {screenLeft_, screenTop_, screenLeft_ + localWidth_, screenTop_ + localHeight_};
}

bool AdjustPanel::ContainsPoint(POINT point) const {
    if (!open_ || !laidOut_) {
        return false;
    }
    const int localX = point.x - screenLeft_;
    const int localY = point.y - screenTop_;
    return localX >= 0 && localX < localWidth_ && localY >= 0 && localY < localHeight_;
}

AdjustPanelHit AdjustPanel::HitTest(POINT point) const {
    AdjustPanelHit hit;
    if (!ContainsPoint(point)) {
        return hit;
    }
    const int localX = point.x - screenLeft_;
    const int localY = point.y - screenTop_;
    if (InsideRect(localX, localY, lockRect_)) {
        hit.kind = AdjustPanelHit::Kind::Lock;
        return hit;
    }
    if (InsideRect(localX, localY, rotateRect_)) {
        hit.kind = AdjustPanelHit::Kind::Rotate;
        return hit;
    }
    // 亮度行：整行（标签右侧）都可点击定位
    RECT row = {sliderTrack_.left, valueRect_.top, valueRect_.right, valueRect_.bottom};
    ::InflateRect(&row, 0, static_cast<int>(4.0f * scale_));
    if (InsideRect(localX, localY, row)) {
        hit.kind = AdjustPanelHit::Kind::Brightness;
        return hit;
    }
    return hit;
}

int AdjustPanel::SliderValueFromLocalX(int localX) const {
    const int trackWidth = static_cast<int>(sliderTrack_.right - sliderTrack_.left);
    if (trackWidth <= 0) {
        return brightness_;
    }
    const int offset = std::max(0, std::min(localX - static_cast<int>(sliderTrack_.left),
                                            trackWidth));
    return offset * 100 / trackWidth;
}

bool AdjustPanel::BrightnessSliderFromPoint(POINT point) {
    if (!open_ || !laidOut_) {
        return false;
    }
    const int value = SliderValueFromLocalX(point.x - screenLeft_);
    if (value == brightness_) {
        return false;
    }
    brightness_ = value;
    UpdateGeometry();
    return true;
}

void AdjustPanel::Render() {
    if (!open_ || !laidOut_) {
        return;
    }
    if (!canvas_.Resize(localWidth_, localHeight_)) {
        return;
    }
    canvas_.Clear();

    const float s = scale_;
    canvas_.FillRoundRect({0, 0, localWidth_, localHeight_},
                          static_cast<int>(16.0f * s), kBorderColor, 255);
    canvas_.FillRoundRect({1, 1, localWidth_ - 1, localHeight_ - 1},
                          static_cast<int>(15.0f * s), kPanelColor, 252);

    const int rowHeight = static_cast<int>(kRowHeight * s);
    const int padding = static_cast<int>(kPadding * s);
    const int labelWidth = static_cast<int>(kLabelWidth * s);
    const int rowGap = static_cast<int>(kRowGap * s);
    const auto rowTop = [&](int index) { return padding + index * (rowHeight + rowGap); };
    const auto labelRect = [&](int index) {
        return RECT{padding, rowTop(index), padding + labelWidth, rowTop(index) + rowHeight};
    };

    const int labelFont = static_cast<int>(15.0f * s);
    const int iconSize = static_cast<int>(20.0f * s);
    const int iconGap = static_cast<int>(6.0f * s);
    const auto drawRowLabel = [&](int index, const wchar_t* icon, const wchar_t* text) {
        RECT rect = labelRect(index);
        const img::Image* image =
            (resources_ != nullptr && icon != nullptr) ? resources_->Get(icon) : nullptr;
        if (image != nullptr && image->Valid()) {
            const int top = (rect.top + rect.bottom) / 2 - iconSize / 2;
            const RECT dest = {rect.left, top, rect.left + iconSize, top + iconSize};
            canvas_.DrawPixels(image->pixels(), image->width(), image->height(), image->stride(),
                               dest);
            rect.left += iconSize + iconGap;
        }
        canvas_.DrawText(text, rect, labelFont, kLabelColor, 255,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    };
    drawRowLabel(0, L"lock.png", L"锁定");
    drawRowLabel(1, L"spin.png", L"旋转");
    drawRowLabel(2, nullptr, L"亮度");

    // 锁定开关
    const bool lockHover = hoverKind_ == AdjustPanelHit::Kind::Lock;
    const int radius = (lockRect_.bottom - lockRect_.top) / 2;
    canvas_.FillRoundRect(lockRect_, radius, locked_ ? kAccentColor : kSwitchOff,
                          lockHover ? 255 : 240);
    const int knob = lockRect_.bottom - lockRect_.top - static_cast<int>(6.0f * s);
    const int knobTop = (lockRect_.top + lockRect_.bottom) / 2 - knob / 2;
    const int knobLeft = locked_ ? lockRect_.right - static_cast<int>(3.0f * s) - knob
                                 : lockRect_.left + static_cast<int>(3.0f * s);
    canvas_.FillRoundRect({knobLeft, knobTop, knobLeft + knob, knobTop + knob}, knob / 2,
                          kSwitchKnob, 255);

    // 旋转按钮：图标已在行标签处呈现，按钮内只保留文字，避免重复
    const bool rotateHover = hoverKind_ == AdjustPanelHit::Kind::Rotate;
    canvas_.FillRoundRect(rotateRect_, static_cast<int>(10.0f * s),
                          rotateHover ? kButtonHover : kButtonColor, 255);
    canvas_.DrawText(L"旋转 90°", rotateRect_, static_cast<int>(14.0f * s), kLabelColor, 255,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 亮度滑块
    const int trackRadius = (sliderTrack_.bottom - sliderTrack_.top) / 2;
    canvas_.FillRoundRect(sliderTrack_, trackRadius, kTrackColor, 255);
    RECT filled = sliderTrack_;
    filled.right = sliderKnob_.left + (sliderKnob_.right - sliderKnob_.left) / 2;
    if (filled.right > filled.left) {
        canvas_.FillRoundRect(filled, trackRadius, kAccentColor, 255);
    }
    canvas_.FillRoundRect(sliderKnob_, (sliderKnob_.bottom - sliderKnob_.top) / 2,
                          kSwitchKnob, 255);
    canvas_.DrawDashedRect(sliderKnob_, kTrackColor, 200, 1,
                           static_cast<int>(6.0f * s), static_cast<int>(4.0f * s));

    const std::wstring valueText =
        deviceBrightness_ ? FormatW(L"%d%%", brightness_) : FormatW(L"%d%%·显示", brightness_);
    canvas_.DrawText(valueText, valueRect_, static_cast<int>(13.0f * s), kValueColor, 255,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

} // namespace ui
} // namespace vb
