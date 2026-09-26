#pragma once

#include <windows.h>

#include <vector>

#include "ui/OverlayCanvas.h"
#include "ui/Resources.h"

namespace vb {
namespace ui {

enum class ToolButtonId : int {
    Select = 0,
    Annotate,
    Erase,
    Rotate,
    Lock,
    Shoot,
    Album,
    Settings,
    Minimize,
    Exit,
    Count,
};

inline constexpr int kToolButtonCount = static_cast<int>(ToolButtonId::Count);

// 底部或两侧功能栏
class Toolbar {
public:
    void Init(Resources* resources, bool vertical);

    void SetScale(float uiScale);
    void SetVertical(bool vertical) { vertical_ = vertical; }
    void Layout(int windowWidth, int windowHeight);

    const RECT& bounds() const { return bounds_; }
    bool vertical() const { return vertical_; }

    int HitTest(POINT point) const;
    ToolButtonId idAt(int index) const;
    const RECT& buttonRect(int index) const;

    void SetActive(int index, bool active);
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    // 覆盖按钮的图标与文字（相册查看时“拍照”变为“返回相机”），传 nullptr 恢复默认
    void SetButtonContent(ToolButtonId id, const wchar_t* icon, const wchar_t* label);
    void ClearButtonContent(ToolButtonId id);

    // 重绘功能栏到内部画布
    void Render(int hoverIndex, int pressedIndex);
    const OverlayCanvas& canvas() const { return canvas_; }

private:
    Resources* resources_ = nullptr;
    bool vertical_ = false;
    float scale_ = 1.0f;
    // 按钮总尺寸超出窗口时的整体缩小比例（1.0 表示按原尺寸绘制）
    float contentScale_ = 1.0f;
    RECT bounds_ = {};
    std::vector<RECT> buttonRects_;
    std::vector<bool> active_;
    const wchar_t* iconOverride_[kToolButtonCount] = {};
    const wchar_t* labelOverride_[kToolButtonCount] = {};
    bool enabled_ = true;
    OverlayCanvas canvas_;
};

} // namespace ui
} // namespace vb
