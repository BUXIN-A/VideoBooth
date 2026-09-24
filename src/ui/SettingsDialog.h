#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "capture/Camera.h"
#include "core/Config.h"
#include "ui/OverlayCanvas.h"

namespace vb {
namespace ui {

// 设置面板：完全自绘，并直接在展台窗口内渲染（不再创建独立窗口）。
// 主窗口为全屏窗口时，系统会按“全屏优化”把画面直接扫描输出，独立设置窗口会被压在
// 画面之下不可见，因此设置界面改为随主窗口一起绘制，由主窗口转发鼠标/键盘输入。
class SettingsDialog {
public:
    bool isOpen() const { return open_; }
    // 需要重绘时返回 true，由调用方上传纹理
    bool dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

    // 打开面板：config 为当前配置副本，点击“保存”后结果写入 result()
    bool Open(HWND owner, const core::AppConfig& config,
              const std::vector<capture::CameraInfo>& cameras, float uiScale, int clientWidth,
              int clientHeight);
    void Close();

    // 主窗口尺寸变化
    void Relayout(int clientWidth, int clientHeight);

    // 输入：坐标为主窗口客户区坐标；返回 true 表示事件已被面板消费（模态期间全部消费）
    bool OnMouseDown(POINT point);
    bool OnMouseMove(POINT point);
    bool OnMouseUp(POINT point);
    bool OnMouseWheel(int delta, POINT point);
    bool OnKeyDown(UINT key);

    bool accepted() const { return accepted_; }
    const core::AppConfig& result() const { return result_; }

    // 面板在客户区中的矩形
    RECT bounds() const { return {originX_, originY_, originX_ + windowWidth_, originY_ + windowHeight_}; }

    // 渲染到内部画布（仅 dirty 时调用）
    void Render();
    const OverlayCanvas& canvas() const { return canvas_; }

private:
    // 可操作字段
    enum class Field {
        None = 0,
        ToolbarPosition,
        FolderBrowse,
        SaveLog,
        Camera,
        Fps,
        Resolution,
        AutoExposure,
        Vsync,
        Antialias,
        DoubleBuffer,
        Cancel,
        Save,
        Close,
        Count,
    };

    // 命中结果
    struct Hit {
        Field field = Field::None;
        int item = -1;       // 下拉列表项下标
        bool onList = false; // 是否命中展开的下拉列表

        bool operator==(const Hit& other) const {
            return field == other.field && item == other.item && onList == other.onList;
        }
        bool operator!=(const Hit& other) const { return !(*this == other); }
    };

    // 下拉选择器数据
    struct Choice {
        Field field = Field::None;
        std::vector<std::wstring> items;
        int selected = 0;
    };

    // 初始化数据模型（选项列表、当前值）
    void BuildModel();
    bool CollectValues();
    void PickFolder();

    // 布局与绘制
    void UpdateLayout();
    RECT FieldRect(Field field) const;
    bool DropdownExpanded(Field field) const { return openField_ == field; }
    Hit HitTest(int localX, int localY) const;

    // 绘制基元
    void DrawCard(const RECT& rect, const std::wstring& title);
    void DrawLabel(const RECT& bounds, const std::wstring& text);
    void DrawDropdown(const RECT& rect, const std::wstring& text, bool expanded, bool hovered);
    void DrawSwitch(const RECT& rect, bool on, bool hovered);
    void DrawButton(const RECT& rect, const std::wstring& text, bool primary, bool hovered);
    void DrawPathField(const RECT& rect, const std::wstring& text, bool hovered);
    void DrawDropdownList();
    void Activate(const Hit& hit);

    int px(float logical) const;
    const Choice* FindChoice(Field field) const;
    Choice* FindChoice(Field field);

    bool open_ = false;
    bool dirty_ = true;
    bool accepted_ = false;
    float uiScale_ = 1.0f;
    HWND owner_ = nullptr;

    core::AppConfig config_;
    core::AppConfig result_;
    std::vector<capture::CameraInfo> cameras_;
    std::vector<std::wstring> cameraIds_; // 与摄像头下拉项一一对应，首项为“自动选择”
    std::wstring folderPath_;

    std::vector<Choice> choices_;
    std::vector<int> fpsValues_;
    std::vector<int> resolutionWidths_;
    std::vector<int> resolutionHeights_;
    bool autoExposure_ = false;
    bool vsync_ = false;
    bool antialias_ = false;
    bool doubleBuffer_ = false;
    bool saveLog_ = false;

    // 几何（画布局部坐标，原点为面板左上角）
    int windowWidth_ = 0;
    int windowHeight_ = 0;
    int originX_ = 0; // 面板左上角在客户区中的位置
    int originY_ = 0;
    int bodyTop_ = 0;
    int bodyBottom_ = 0;
    int contentHeight_ = 0;
    int maxScroll_ = 0;
    int scrollY_ = 0;
    RECT closeRect_ = {0, 0, 0, 0};
    RECT cancelRect_ = {0, 0, 0, 0};
    RECT saveRect_ = {0, 0, 0, 0};
    RECT fieldRects_[static_cast<int>(Field::Count)] = {};
    RECT subRects_[static_cast<int>(Field::Count)] = {}; // 附加区域（如“选择文件夹”按钮）
    RECT listRect_ = {0, 0, 0, 0};
    std::vector<RECT> listItemRects_;

    Field openField_ = Field::None;
    Hit hover_;
    Hit pressed_;

    OverlayCanvas canvas_;
};

} // namespace ui
} // namespace vb
