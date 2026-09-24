#include "ui/Toolbar.h"

#include "util/Log.h"

#include <algorithm>

namespace vb {
namespace ui {
namespace {

struct ToolButton {
    ToolButtonId id;
    const wchar_t* icon;
    const wchar_t* label;
};

const ToolButton kButtons[] = {
    {ToolButtonId::Select, L"pointer.png", L"选择"},
    {ToolButtonId::Annotate, L"pen.png", L"批注"},
    {ToolButtonId::Erase, L"eraser.png", L"橡皮"},
    {ToolButtonId::Rotate, L"spin.png", L"旋转"},
    {ToolButtonId::Lock, L"lock.png", L"锁定"},
    {ToolButtonId::Shoot, L"shoot.png", L"拍照"},
    {ToolButtonId::Album, L"album.png", L"相册"},
    {ToolButtonId::Settings, L"setting.png", L"设置"},
    {ToolButtonId::Minimize, L"minimize.png", L"最小化"},
    {ToolButtonId::Exit, L"exit.png", L"退出"},
};

constexpr int kGroupBoundaries[] = {2, 6}; // 在这些索引之后增加组间距

bool IsGroupBoundary(int index) {
    for (const int boundary : kGroupBoundaries) {
        if (index == boundary) {
            return true;
        }
    }
    return false;
}

constexpr COLORREF kBarColor = RGB(238, 238, 238);
constexpr COLORREF kHoverColor = RGB(255, 255, 255);
constexpr COLORREF kPressColor = RGB(206, 206, 206);
constexpr COLORREF kActiveColor = RGB(45, 127, 249);
constexpr COLORREF kLabelColor = RGB(58, 58, 58);
constexpr COLORREF kActiveLabelColor = RGB(21, 92, 205);

} // namespace

bool Toolbar::Init(Resources* resources, bool vertical) {
    resources_ = resources;
    vertical_ = vertical;
    buttonRects_.assign(kToolButtonCount, RECT{0, 0, 0, 0});
    active_.assign(kToolButtonCount, false);
    return true;
}

void Toolbar::SetScale(float uiScale) {
    scale_ = std::max(0.5f, uiScale);
}

void Toolbar::Layout(int windowWidth, int windowHeight) {
    const float s = scale_;
    const int buttonWidth = static_cast<int>(78.0f * s);
    const int buttonHeight = static_cast<int>(84.0f * s);
    const int gap = static_cast<int>(6.0f * s);
    const int groupGap = static_cast<int>(18.0f * s);
    const int padding = static_cast<int>(14.0f * s);
    const int margin = static_cast<int>(14.0f * s);

    int totalLength = padding * 2;
    for (int i = 0; i < kToolButtonCount; ++i) {
        totalLength += (vertical_ ? buttonHeight : buttonWidth);
        if (i + 1 < kToolButtonCount) {
            totalLength += IsGroupBoundary(i) ? groupGap : gap;
        }
    }

    if (vertical_) {
        const int barWidth = buttonWidth + padding * 2;
        const int barHeight = totalLength;
        const int left = windowWidth - margin - barWidth;
        const int top = std::max(margin, (windowHeight - barHeight) / 2);
        bounds_ = {left, top, left + barWidth, top + barHeight};

        int cursor = top + padding;
        for (int i = 0; i < kToolButtonCount; ++i) {
            buttonRects_[i] = {left + padding, cursor, left + padding + buttonWidth,
                               cursor + buttonHeight};
            cursor += buttonHeight;
            if (i + 1 < kToolButtonCount) {
                cursor += IsGroupBoundary(i) ? groupGap : gap;
            }
        }
    } else {
        const int barWidth = totalLength;
        const int barHeight = buttonHeight + padding * 2;
        const int left = (windowWidth - barWidth) / 2;
        const int top = windowHeight - margin - barHeight;
        bounds_ = {left, top, left + barWidth, top + barHeight};

        int cursor = left + padding;
        for (int i = 0; i < kToolButtonCount; ++i) {
            buttonRects_[i] = {cursor, top + padding, cursor + buttonWidth,
                               top + padding + buttonHeight};
            cursor += buttonWidth;
            if (i + 1 < kToolButtonCount) {
                cursor += IsGroupBoundary(i) ? groupGap : gap;
            }
        }
    }
}

int Toolbar::HitTest(POINT point) const {
    for (int i = 0; i < kToolButtonCount; ++i) {
        const RECT& rect = buttonRects_[i];
        if (point.x >= rect.left && point.x < rect.right && point.y >= rect.top &&
            point.y < rect.bottom) {
            return i;
        }
    }
    return -1;
}

ToolButtonId Toolbar::idAt(int index) const {
    if (index < 0 || index >= kToolButtonCount) {
        return ToolButtonId::Count;
    }
    return kButtons[index].id;
}

const RECT& Toolbar::buttonRect(int index) const {
    static const RECT kEmpty = {0, 0, 0, 0};
    if (index < 0 || index >= kToolButtonCount) {
        return kEmpty;
    }
    return buttonRects_[index];
}

void Toolbar::SetActive(int index, bool active) {
    if (index >= 0 && index < kToolButtonCount) {
        active_[index] = active;
    }
}

bool Toolbar::isActive(int index) const {
    if (index < 0 || index >= kToolButtonCount) {
        return false;
    }
    return active_[index];
}

void Toolbar::SetButtonContent(ToolButtonId id, const wchar_t* icon, const wchar_t* label) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kToolButtonCount) {
        return;
    }
    iconOverride_[index] = icon;
    labelOverride_[index] = label;
}

void Toolbar::ClearButtonContent(ToolButtonId id) {
    SetButtonContent(id, nullptr, nullptr);
}

void Toolbar::Render(int hoverIndex, int pressedIndex) {
    const int width = bounds_.right - bounds_.left;
    const int height = bounds_.bottom - bounds_.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (!canvas_.Resize(width, height)) {
        return;
    }
    canvas_.Clear();

    const float s = scale_;
    const int radius = static_cast<int>(16.0f * s);
    const RECT barRect = {0, 0, width, height};
    canvas_.FillRoundRect(barRect, radius, kBarColor, 236);

    const int iconSize = static_cast<int>(46.0f * s);
    const int labelFont = static_cast<int>(15.0f * s);
    const int iconTop = static_cast<int>(8.0f * s);
    const RECT iconRect = {0, iconTop, iconSize, iconTop + iconSize};

    for (int i = 0; i < kToolButtonCount; ++i) {
        const RECT& rect = buttonRects_[i];
        const int localLeft = rect.left - bounds_.left;
        const int localTop = rect.top - bounds_.top;
        const int localWidth = rect.right - rect.left;
        const int localHeight = rect.bottom - rect.top;
        if (localLeft < 0 || localTop < 0) {
            continue;
        }

        const bool isActive = active_[i];
        const bool isHover = (i == hoverIndex) && enabled_;
        const bool isPressed = (i == pressedIndex) && enabled_;

        if (isActive || isHover || isPressed) {
            RECT highlight = {localLeft + static_cast<int>(3.0f * s),
                              localTop + static_cast<int>(3.0f * s),
                              localLeft + localWidth - static_cast<int>(3.0f * s),
                              localTop + localHeight - static_cast<int>(3.0f * s)};
            const COLORREF color = isPressed ? kPressColor : (isActive ? kActiveColor : kHoverColor);
            const BYTE alpha = isPressed ? static_cast<BYTE>(230)
                                         : (isActive ? static_cast<BYTE>(48)
                                                     : static_cast<BYTE>(200));
            canvas_.FillRoundRect(highlight, static_cast<int>(12.0f * s), color, alpha);
        }

        const BYTE contentAlpha = enabled_ ? 255 : 110;

        const wchar_t* iconName =
            iconOverride_[i] != nullptr ? iconOverride_[i] : kButtons[i].icon;
        const wchar_t* label = labelOverride_[i] != nullptr ? labelOverride_[i] : kButtons[i].label;

        const img::Image* icon =
            resources_ != nullptr ? resources_->Get(iconName) : nullptr;
        if (icon != nullptr && icon->Valid()) {
            RECT dest = iconRect;
            const int offsetX = localLeft + (localWidth - iconSize) / 2;
            const int offsetY = localTop + iconTop;
            dest = {offsetX, offsetY, offsetX + iconSize, offsetY + iconSize};
            canvas_.DrawPixels(icon->pixels(), icon->width(), icon->height(), icon->stride(),
                               dest, 0, contentAlpha);
        }

        const RECT textRect = {localLeft, localTop + iconRect.bottom,
                               localLeft + localWidth,
                               localTop + localHeight - static_cast<int>(2.0f * s)};
        canvas_.DrawText(label, textRect, labelFont,
                         isActive ? kActiveLabelColor : kLabelColor, contentAlpha);
    }
}

} // namespace ui
} // namespace vb
