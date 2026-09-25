#include "ui/SettingsDialog.h"

#include "core/Paths.h"
#include "ui/FileDialog.h"
#include "util/Log.h"
#include "util/Strings.h"

#include <algorithm>

namespace vb {
namespace ui {
namespace {

// ---- 逻辑布局（按 UI 比例缩放） ----
constexpr float kWindowWidth = 640.0f;
constexpr float kTitleBarHeight = 56.0f;
constexpr float kFooterHeight = 76.0f;
constexpr float kMargin = 22.0f;
constexpr float kCardPaddingX = 18.0f;
constexpr float kCardPaddingY = 18.0f;
constexpr float kCardTitleHeight = 24.0f;
constexpr float kCardTitleGap = 12.0f;
constexpr float kRowHeight = 46.0f;
constexpr float kRowPitch = 52.0f;
constexpr float kFirstRowOffset = 54.0f; // 卡片内首行相对卡片顶
constexpr float kLabelWidth = 116.0f;
constexpr float kLabelControlGap = 12.0f;
constexpr float kControlHeight = 38.0f;
constexpr float kSwitchWidth = 46.0f;
constexpr float kSwitchHeight = 26.0f;
constexpr float kButtonWidth = 108.0f;
constexpr float kButtonHeight = 44.0f;
constexpr float kButtonGap = 12.0f;
constexpr float kCloseSize = 26.0f;
constexpr float kListItemHeight = 36.0f;
constexpr float kListPadding = 5.0f;
constexpr float kWindowRadius = 18.0f;
constexpr float kCardRadius = 14.0f;
constexpr float kControlRadius = 9.0f;
constexpr float kButtonRadius = 10.0f;

// 卡片纵向布局（内容坐标）
constexpr float kCard1Top = 0.0f;
constexpr float kCard1Height = 222.0f;
constexpr float kCard2Top = 236.0f; // 222 + 14
constexpr float kCard2Height = 274.0f;
constexpr float kCard3Top = 524.0f; // 236 + 274 + 14
constexpr float kCard3Height = 222.0f;
constexpr float kContentHeight = 746.0f;

// ---- 配色 ----
constexpr COLORREF kWindowColor = RGB(255, 255, 255);
constexpr COLORREF kWindowBorder = RGB(224, 227, 233);
constexpr COLORREF kCardColor = RGB(246, 247, 249);
constexpr COLORREF kCardTitleColor = RGB(132, 138, 148);
constexpr COLORREF kLabelColor = RGB(74, 80, 90);
constexpr COLORREF kValueColor = RGB(28, 32, 38);
constexpr COLORREF kControlBack = RGB(255, 255, 255);
constexpr COLORREF kControlBorder = RGB(219, 223, 229);
constexpr COLORREF kAccent = RGB(45, 127, 249);
constexpr COLORREF kAccentSoft = RGB(233, 241, 254);
constexpr COLORREF kSwitchOff = RGB(203, 208, 215);
constexpr COLORREF kSwitchKnob = RGB(255, 255, 255);
constexpr COLORREF kButtonBack = RGB(240, 242, 245);
constexpr COLORREF kButtonHover = RGB(228, 232, 239);
constexpr COLORREF kHintColor = RGB(152, 158, 168);
constexpr COLORREF kCloseHover = RGB(238, 240, 244);
constexpr COLORREF kScrollBar = RGB(206, 210, 216);
constexpr COLORREF kListBorder = RGB(213, 218, 226);

constexpr int kFpsPresets[] = {10, 15, 20, 24, 25, 30, 50, 60};

struct ResolutionPreset {
    int width;
    int height;
};

constexpr ResolutionPreset kResolutionPresets[] = {
    {640, 480},   {800, 600},   {1024, 768},  {1280, 720},  {1600, 900},
    {1920, 1080}, {2560, 1440}, {3840, 2160},
};

bool InsideRect(int x, int y, const RECT& rect) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

std::wstring Trim(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

// 简化的路径中间省略，保证首尾可见
std::wstring ElideMiddle(const std::wstring& text, size_t limit) {
    if (text.size() <= limit || limit < 6) {
        return text;
    }
    const size_t head = limit / 2 - 1;
    const size_t tail = limit - head - 1;
    return text.substr(0, head) + L"…" + text.substr(text.size() - tail);
}

void FillTriangleDown(OverlayCanvas& canvas, int centerX, int top, int size, COLORREF color) {
    for (int i = 0; i < size; ++i) {
        const int half = (size - i) / 2;
        const RECT row = {centerX - half, top + i, centerX + half + 1, top + i + 1};
        canvas.FillRect(row, color, 255);
    }
}

} // namespace

int SettingsDialog::px(float logical) const {
    return static_cast<int>(logical * uiScale_ + 0.5f);
}

const SettingsDialog::Choice* SettingsDialog::FindChoice(Field field) const {
    for (const Choice& choice : choices_) {
        if (choice.field == field) {
            return &choice;
        }
    }
    return nullptr;
}

SettingsDialog::Choice* SettingsDialog::FindChoice(Field field) {
    for (Choice& choice : choices_) {
        if (choice.field == field) {
            return &choice;
        }
    }
    return nullptr;
}

void SettingsDialog::Open(HWND owner, const core::AppConfig& config,
                          const std::vector<capture::CameraInfo>& cameras, float uiScale,
                          int clientWidth, int clientHeight) {
    owner_ = owner;
    config_ = config;
    result_ = config;
    cameras_ = cameras;
    uiScale_ = uiScale > 0.0f ? uiScale : 1.0f;

    accepted_ = false;
    open_ = true;
    dirty_ = true;
    openField_ = Field::None;
    scrollY_ = 0;
    hover_ = Hit();
    pressed_ = Hit();

    BuildModel();
    Relayout(clientWidth, clientHeight);
}

void SettingsDialog::Close() {
    open_ = false;
    hover_ = Hit();
    pressed_ = Hit();
    openField_ = Field::None;
    canvas_.Release();
}

void SettingsDialog::Relayout(int clientWidth, int clientHeight) {
    if (clientWidth <= 0 || clientHeight <= 0) {
        return;
    }
    const int margin = px(24.0f);
    windowWidth_ = px(kWindowWidth);
    // 依据客户区高度决定面板高度，内容超出时可滚动
    const int desiredHeight = px(kTitleBarHeight + kContentHeight + kFooterHeight);
    windowHeight_ = std::min(desiredHeight, std::max(px(400.0f), clientHeight - margin * 2));
    originX_ = (clientWidth - windowWidth_) / 2;
    originY_ = (clientHeight - windowHeight_) / 2;
    UpdateLayout();
    dirty_ = true;
}

bool SettingsDialog::OnMouseDown(POINT point) {
    if (!open_) {
        return false;
    }
    pressed_ = HitTest(point.x - originX_, point.y - originY_);
    return true;
}

bool SettingsDialog::OnMouseMove(POINT point) {
    if (!open_) {
        return false;
    }
    const Hit hit = HitTest(point.x - originX_, point.y - originY_);
    if (hit != hover_) {
        hover_ = hit;
        dirty_ = true;
    }
    return true;
}

bool SettingsDialog::OnMouseUp(POINT point) {
    if (!open_) {
        return false;
    }
    const Hit hit = HitTest(point.x - originX_, point.y - originY_);
    const Hit pressed = pressed_;
    pressed_ = Hit();
    if (pressed.field == hit.field && pressed.item == hit.item && pressed.onList == hit.onList &&
        pressed.field != Field::None) {
        Activate(pressed);
    } else if (openField_ != Field::None) {
        // 点击其他位置收起展开的下拉列表
        openField_ = Field::None;
        dirty_ = true;
    }
    if (open_) {
        UpdateLayout();
        dirty_ = true;
    }
    return true;
}

bool SettingsDialog::OnMouseWheel(int delta, POINT point) {
    (void)point; // 面板为模态浮层，滚轮一律用于内容滚动
    if (!open_) {
        return false;
    }
    if (maxScroll_ > 0) {
        scrollY_ = std::max(0, std::min(scrollY_ - delta / WHEEL_DELTA * px(60.0f), maxScroll_));
        openField_ = Field::None;
        UpdateLayout();
        dirty_ = true;
    }
    return true;
}

bool SettingsDialog::OnKeyDown(UINT key) {
    if (!open_) {
        return false;
    }
    if (key == VK_ESCAPE) {
        accepted_ = false;
        Close();
        return true;
    }
    if (key == VK_RETURN && CollectValues()) {
        accepted_ = true;
        Close();
        return true;
    }
    return true;
}

void SettingsDialog::BuildModel() {
    const core::AppConfig& config = config_;

    choices_.clear();

    Choice toolbar;
    toolbar.field = Field::ToolbarPosition;
    toolbar.items = {L"底部（横向）", L"两侧（纵向）"};
    toolbar.selected = config.IsToolbarVertical() ? 1 : 0;
    choices_.push_back(toolbar);

    Choice camera;
    camera.field = Field::Camera;
    cameraIds_.clear();
    cameraIds_.emplace_back();
    camera.items.emplace_back(L"自动选择（第一个可用设备）");
    for (const capture::CameraInfo& device : cameras_) {
        cameraIds_.push_back(device.id);
        camera.items.push_back(device.name);
    }
    camera.selected = 0;
    for (size_t i = 1; i < cameraIds_.size(); ++i) {
        if (EqualsIgnoreCase(cameraIds_[i], config.camera.defaultCamera)) {
            camera.selected = static_cast<int>(i);
            break;
        }
    }
    choices_.push_back(camera);

    fpsValues_.assign(std::begin(kFpsPresets), std::end(kFpsPresets));
    if (std::find(fpsValues_.begin(), fpsValues_.end(), config.camera.fps) == fpsValues_.end()) {
        fpsValues_.push_back(config.camera.fps);
    }
    std::sort(fpsValues_.begin(), fpsValues_.end());
    Choice fps;
    fps.field = Field::Fps;
    for (size_t i = 0; i < fpsValues_.size(); ++i) {
        fps.items.push_back(FormatW(L"%d fps", fpsValues_[i]));
        if (fpsValues_[i] == config.camera.fps) {
            fps.selected = static_cast<int>(i);
        }
    }
    choices_.push_back(fps);

    resolutionWidths_.clear();
    resolutionHeights_.clear();
    for (const ResolutionPreset& preset : kResolutionPresets) {
        resolutionWidths_.push_back(preset.width);
        resolutionHeights_.push_back(preset.height);
    }
    if (std::find(resolutionWidths_.begin(), resolutionWidths_.end(), config.camera.width) ==
        resolutionWidths_.end()) {
        resolutionWidths_.push_back(config.camera.width);
        resolutionHeights_.push_back(config.camera.height);
    }
    Choice resolution;
    resolution.field = Field::Resolution;
    for (size_t i = 0; i < resolutionWidths_.size(); ++i) {
        resolution.items.push_back(FormatW(L"%d × %d", resolutionWidths_[i], resolutionHeights_[i]));
        if (resolutionWidths_[i] == config.camera.width &&
            resolutionHeights_[i] == config.camera.height) {
            resolution.selected = static_cast<int>(i);
        }
    }
    choices_.push_back(resolution);

    folderPath_ = config.tempFolder.empty() ? paths::DefaultPhotoDir() : config.tempFolder;
    autoExposure_ = config.camera.autoExposure;
    vsync_ = config.render.vsync;
    antialias_ = config.render.antialias;
    doubleBuffer_ = config.render.doubleBuffer;
    saveLog_ = config.saveLog;
}

void SettingsDialog::PickFolder() {
    std::wstring folder;
    if (PickFolderDialog(owner_, folderPath_, folder)) {
        folderPath_ = folder;
        openField_ = Field::None;
        dirty_ = true;
    }
}

bool SettingsDialog::CollectValues() {
    core::AppConfig config = config_;

    const Choice* toolbar = FindChoice(Field::ToolbarPosition);
    config.toolbarPosition = (toolbar != nullptr && toolbar->selected == 1) ? "sides" : "bottom";

    // 临时文件夹：与默认目录一致时保持“自动”语义
    const std::wstring folder = Trim(folderPath_);
    if (!folder.empty() && !EqualsIgnoreCase(folder, paths::DefaultPhotoDir())) {
        if (!paths::EnsureDirectory(folder)) {
            ::MessageBoxW(owner_, L"所选临时文件夹不可用，请重新选择。", L"设置",
                          MB_ICONWARNING | MB_OK);
            return false;
        }
        config.tempFolder = folder;
    } else {
        config.tempFolder.clear();
    }

    const Choice* camera = FindChoice(Field::Camera);
    if (camera != nullptr && camera->selected >= 0 &&
        static_cast<size_t>(camera->selected) < cameraIds_.size()) {
        config.camera.defaultCamera = cameraIds_[static_cast<size_t>(camera->selected)];
    } else {
        config.camera.defaultCamera.clear();
    }

    const Choice* fps = FindChoice(Field::Fps);
    if (fps != nullptr && fps->selected >= 0 &&
        static_cast<size_t>(fps->selected) < fpsValues_.size()) {
        config.camera.fps = fpsValues_[static_cast<size_t>(fps->selected)];
    }

    const Choice* resolution = FindChoice(Field::Resolution);
    if (resolution != nullptr && resolution->selected >= 0 &&
        static_cast<size_t>(resolution->selected) < resolutionWidths_.size()) {
        config.camera.width = resolutionWidths_[static_cast<size_t>(resolution->selected)];
        config.camera.height = resolutionHeights_[static_cast<size_t>(resolution->selected)];
    }

    config.camera.autoExposure = autoExposure_;
    config.render.vsync = vsync_;
    config.render.antialias = antialias_;
    config.render.doubleBuffer = doubleBuffer_;
    config.saveLog = saveLog_;

    result_ = config;
    return true;
}

void SettingsDialog::UpdateLayout() {
    const auto P = [this](float value) { return px(value); };

    bodyTop_ = P(kTitleBarHeight);
    bodyBottom_ = windowHeight_ - P(kFooterHeight);
    const int contentHeight = P(kContentHeight);
    maxScroll_ = std::max(0, contentHeight - (bodyBottom_ - bodyTop_));
    scrollY_ = std::max(0, std::min(scrollY_, maxScroll_));

    const int cardLeft = P(kMargin);
    const int cardRight = windowWidth_ - P(kMargin);
    const int innerLeft = cardLeft + P(kCardPaddingX);
    const int innerRight = cardRight - P(kCardPaddingX);
    const int controlLeft = innerLeft + P(kLabelWidth + kLabelControlGap);
    const int controlTopOffset = P((kRowHeight - kControlHeight) * 0.5f);
    const int switchOffset = P((kRowHeight - kSwitchHeight) * 0.5f);

    // 内容坐标 → 窗口坐标
    const auto rowTop = [&](float cardTop, int rowIndex) {
        const int contentY = P(cardTop + kFirstRowOffset + rowIndex * kRowPitch);
        return bodyTop_ - scrollY_ + contentY;
    };
    const auto fieldRect = [&](float cardTop, int rowIndex, int left, int top, int right,
                               int bottom) {
        const int baseY = rowTop(cardTop, rowIndex);
        return RECT{left, baseY + top, right, baseY + bottom};
    };

    // 关闭按钮与底部按钮（不随内容滚动）
    closeRect_ = {windowWidth_ - P(kMargin) - P(kCloseSize), P((kTitleBarHeight - kCloseSize) * 0.5f),
                  windowWidth_ - P(kMargin), P((kTitleBarHeight + kCloseSize) * 0.5f)};
    const int footerTop = windowHeight_ - P(kFooterHeight);
    saveRect_ = {cardRight - P(kButtonWidth), footerTop + P(18.0f), cardRight,
                 footerTop + P(18.0f + kButtonHeight)};
    cancelRect_ = {saveRect_.left - P(kButtonGap) - P(kButtonWidth), saveRect_.top,
                   saveRect_.left - P(kButtonGap), saveRect_.bottom};

    // 基础
    fieldRects_[static_cast<int>(Field::ToolbarPosition)] =
        fieldRect(kCard1Top, 0, controlLeft, controlTopOffset, controlLeft + P(220.0f),
                  controlTopOffset + P(kControlHeight));

    const int browseWidth = P(kButtonWidth);
    const int pathWidth = innerRight - controlLeft - browseWidth - P(10.0f);
    fieldRects_[static_cast<int>(Field::FolderBrowse)] =
        fieldRect(kCard1Top, 1, controlLeft, controlTopOffset, controlLeft + pathWidth,
                  controlTopOffset + P(kControlHeight));
    subRects_[static_cast<int>(Field::FolderBrowse)] =
        fieldRect(kCard1Top, 1, controlLeft + pathWidth + P(10.0f), controlTopOffset, innerRight,
                  controlTopOffset + P(kControlHeight));
    fieldRects_[static_cast<int>(Field::SaveLog)] =
        fieldRect(kCard1Top, 2, controlLeft, switchOffset, controlLeft + P(kSwitchWidth),
                  switchOffset + P(kSwitchHeight));

    // 画面
    fieldRects_[static_cast<int>(Field::Camera)] =
        fieldRect(kCard2Top, 0, controlLeft, controlTopOffset, innerRight,
                  controlTopOffset + P(kControlHeight));
    fieldRects_[static_cast<int>(Field::Fps)] =
        fieldRect(kCard2Top, 1, controlLeft, controlTopOffset, controlLeft + P(160.0f),
                  controlTopOffset + P(kControlHeight));
    fieldRects_[static_cast<int>(Field::Resolution)] =
        fieldRect(kCard2Top, 2, controlLeft, controlTopOffset, controlLeft + P(220.0f),
                  controlTopOffset + P(kControlHeight));
    fieldRects_[static_cast<int>(Field::AutoExposure)] =
        fieldRect(kCard2Top, 3, controlLeft, switchOffset, controlLeft + P(kSwitchWidth),
                  switchOffset + P(kSwitchHeight));

    // 渲染
    fieldRects_[static_cast<int>(Field::Vsync)] =
        fieldRect(kCard3Top, 0, controlLeft, switchOffset, controlLeft + P(kSwitchWidth),
                  switchOffset + P(kSwitchHeight));
    fieldRects_[static_cast<int>(Field::Antialias)] =
        fieldRect(kCard3Top, 1, controlLeft, switchOffset, controlLeft + P(kSwitchWidth),
                  switchOffset + P(kSwitchHeight));
    fieldRects_[static_cast<int>(Field::DoubleBuffer)] =
        fieldRect(kCard3Top, 2, controlLeft, switchOffset, controlLeft + P(kSwitchWidth),
                  switchOffset + P(kSwitchHeight));

    // 展开的下拉列表
    listRect_ = {0, 0, 0, 0};
    listItemRects_.clear();
    if (openField_ != Field::None) {
        const Choice* choice = FindChoice(openField_);
        const RECT anchor = fieldRects_[static_cast<int>(openField_)];
        if (choice != nullptr && anchor.right > anchor.left) {
            int listWidth = anchor.right - anchor.left;
            for (const std::wstring& item : choice->items) {
                const int textWidth = canvas_.MeasureText(item, P(15.0f)).cx;
                listWidth = std::max(listWidth, textWidth + P(48.0f));
            }
            listWidth = std::min(listWidth, windowWidth_ - P(kMargin) * 2);
            int left = anchor.left;
            const int maxLeft = windowWidth_ - P(kMargin) - listWidth;
            left = std::max(P(kMargin), std::min(left, maxLeft));

            const int listHeight =
                static_cast<int>(choice->items.size()) * P(kListItemHeight) + P(kListPadding) * 2;
            int top = anchor.bottom + P(6.0f);
            // 下方空间不足时向上展开
            if (top + listHeight > windowHeight_ - P(kFooterHeight) - P(6.0f)) {
                top = anchor.top - P(6.0f) - listHeight;
            }
            top = std::max(P(4.0f), top);
            listRect_ = {left, top, left + listWidth, top + listHeight};

            for (size_t i = 0; i < choice->items.size(); ++i) {
                const int itemTop = top + P(kListPadding) + static_cast<int>(i) * P(kListItemHeight);
                listItemRects_.push_back(
                    RECT{left + P(kListPadding), itemTop, left + listWidth - P(kListPadding),
                         itemTop + P(kListItemHeight)});
            }
        }
    }
}

RECT SettingsDialog::FieldRect(Field field) const {
    const int index = static_cast<int>(field);
    if (index < 0 || index >= static_cast<int>(Field::Count)) {
        return RECT{0, 0, 0, 0};
    }
    return fieldRects_[index];
}

SettingsDialog::Hit SettingsDialog::HitTest(int x, int y) const {
    Hit hit;
    // 展开的下拉列表优先响应
    if (openField_ != Field::None) {
        for (size_t i = 0; i < listItemRects_.size(); ++i) {
            if (InsideRect(x, y, listItemRects_[i])) {
                hit.field = openField_;
                hit.item = static_cast<int>(i);
                hit.onList = true;
                return hit;
            }
        }
    }
    for (int i = 1; i < static_cast<int>(Field::Count); ++i) {
        const Field field = static_cast<Field>(i);
        if (InsideRect(x, y, fieldRects_[i])) {
            hit.field = field;
            return hit;
        }
    }
    const RECT browse = subRects_[static_cast<int>(Field::FolderBrowse)];
    if (InsideRect(x, y, browse)) {
        hit.field = Field::FolderBrowse;
        return hit;
    }
    if (InsideRect(x, y, cancelRect_)) {
        hit.field = Field::Cancel;
        return hit;
    }
    if (InsideRect(x, y, saveRect_)) {
        hit.field = Field::Save;
        return hit;
    }
    if (InsideRect(x, y, closeRect_)) {
        hit.field = Field::Close;
        return hit;
    }
    return hit;
}

void SettingsDialog::Activate(const Hit& hit) {
    if (hit.onList) {
        Choice* choice = FindChoice(hit.field);
        if (choice != nullptr && hit.item >= 0 &&
            static_cast<size_t>(hit.item) < choice->items.size()) {
            choice->selected = hit.item;
            VB_INFO("设置项选择: %ls", choice->items[static_cast<size_t>(hit.item)].c_str());
        }
        openField_ = Field::None;
        return;
    }

    switch (hit.field) {
    case Field::ToolbarPosition:
    case Field::Camera:
    case Field::Fps:
    case Field::Resolution:
        openField_ = (openField_ == hit.field) ? Field::None : hit.field;
        break;
    case Field::AutoExposure:
        autoExposure_ = !autoExposure_;
        break;
    case Field::SaveLog:
        saveLog_ = !saveLog_;
        break;
    case Field::Vsync:
        vsync_ = !vsync_;
        break;
    case Field::Antialias:
        antialias_ = !antialias_;
        break;
    case Field::DoubleBuffer:
        doubleBuffer_ = !doubleBuffer_;
        break;
    case Field::FolderBrowse:
        PickFolder();
        break;
    case Field::Save:
        if (CollectValues()) {
            accepted_ = true;
            Close();
        }
        break;
    case Field::Cancel:
    case Field::Close:
        accepted_ = false;
        Close();
        break;
    default:
        break;
    }
}

void SettingsDialog::DrawCard(const RECT& rect, const std::wstring& title) {
    canvas_.FillRoundRect(rect, px(kCardRadius), kCardColor, 255);
    const RECT titleRect = {rect.left + px(kCardPaddingX), rect.top + px(kCardPaddingY),
                            rect.right - px(kCardPaddingX),
                            rect.top + px(kCardPaddingY + kCardTitleHeight)};
    canvas_.DrawText(title, titleRect, px(13.0f), kCardTitleColor, 255,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void SettingsDialog::DrawLabel(const RECT& bounds, const std::wstring& text) {
    canvas_.DrawText(text, bounds, px(15.0f), kLabelColor, 255,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void SettingsDialog::DrawDropdown(const RECT& rect, const std::wstring& text, bool expanded,
                                  bool hovered) {
    const bool active = expanded || hovered;
    // 1px 边框：外圈描边后内缩填充
    canvas_.FillRoundRect(rect, px(kControlRadius), active ? kAccent : kControlBorder, 255);
    RECT inner = {rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1};
    canvas_.FillRoundRect(inner, px(kControlRadius) - 1, active ? kAccentSoft : kControlBack, 255);

    const int textLeft = rect.left + px(14.0f);
    const int arrowCenter = rect.right - px(18.0f);
    const RECT textRect = {textLeft, rect.top, arrowCenter - px(12.0f), rect.bottom};
    canvas_.DrawText(text, textRect, px(15.0f), kValueColor, 255,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    FillTriangleDown(canvas_, arrowCenter, rect.top + px(17.0f), px(9.0f),
                     active ? kAccent : RGB(140, 146, 156));
}

void SettingsDialog::DrawSwitch(const RECT& rect, bool on, bool hovered) {
    const int radius = (rect.bottom - rect.top) / 2;
    canvas_.FillRoundRect(rect, radius, on ? kAccent : kSwitchOff, hovered ? 255 : 240);
    const int knob = rect.bottom - rect.top - px(6.0f);
    const int knobTop = (rect.top + rect.bottom) / 2 - knob / 2;
    const int knobLeft = on ? rect.right - px(3.0f) - knob : rect.left + px(3.0f);
    canvas_.FillRoundRect({knobLeft, knobTop, knobLeft + knob, knobTop + knob}, knob / 2,
                          kSwitchKnob, 255);
}

void SettingsDialog::DrawButton(const RECT& rect, const std::wstring& text, bool primary,
                                bool hovered) {
    COLORREF back = primary ? kAccent : kButtonBack;
    if (hovered) {
        back = primary ? RGB(28, 108, 226) : kButtonHover;
    }
    canvas_.FillRoundRect(rect, px(kButtonRadius), back, 255);
    canvas_.DrawText(text, rect, px(15.0f), primary ? RGB(255, 255, 255) : RGB(58, 64, 74), 255);
}

void SettingsDialog::DrawPathField(const RECT& rect, const std::wstring& text, bool hovered) {
    canvas_.FillRoundRect(rect, px(kControlRadius), hovered ? kAccent : kControlBorder, 255);
    RECT inner = {rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1};
    canvas_.FillRoundRect(inner, px(kControlRadius) - 1, kControlBack, 255);

    const RECT textRect = {rect.left + px(14.0f), rect.top, rect.right - px(12.0f), rect.bottom};
    canvas_.DrawText(ElideMiddle(text, 46), textRect, px(15.0f), kValueColor, 255,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void SettingsDialog::DrawDropdownList() {
    if (openField_ == Field::None || listItemRects_.empty()) {
        return;
    }
    const Choice* choice = FindChoice(openField_);
    if (choice == nullptr) {
        return;
    }
    canvas_.FillRoundRect(listRect_, px(12.0f), kListBorder, 255);
    RECT inner = {listRect_.left + 1, listRect_.top + 1, listRect_.right - 1, listRect_.bottom - 1};
    canvas_.FillRoundRect(inner, px(12.0f) - 1, kControlBack, 255);

    for (size_t i = 0; i < listItemRects_.size(); ++i) {
        const RECT& item = listItemRects_[i];
        const bool selected = static_cast<int>(i) == choice->selected;
        const bool hovered = hover_.onList && hover_.item == static_cast<int>(i);
        if (selected) {
            canvas_.FillRoundRect(item, px(8.0f), kAccentSoft, 255);
        } else if (hovered) {
            canvas_.FillRoundRect(item, px(8.0f), kButtonBack, 255);
        }
        const RECT textRect = {item.left + px(12.0f), item.top, item.right - px(10.0f), item.bottom};
        canvas_.DrawText(choice->items[i], textRect, px(15.0f),
                         selected ? kAccent : kValueColor, 255,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void SettingsDialog::Render() {
    if (windowWidth_ <= 0 || windowHeight_ <= 0) {
        return;
    }
    if (!canvas_.Resize(windowWidth_, windowHeight_)) {
        return;
    }
    canvas_.Clear();

    const RECT window = {0, 0, windowWidth_, windowHeight_};
    canvas_.FillRoundRect(window, px(kWindowRadius), kWindowBorder, 255);
    RECT inner = {1, 1, windowWidth_ - 1, windowHeight_ - 1};
    canvas_.FillRoundRect(inner, px(kWindowRadius) - 1, kWindowColor, 255);

    // 标题栏
    canvas_.FillRect({0, bodyTop_ - 1, windowWidth_, bodyTop_}, kWindowBorder, 255);
    const RECT titleRect = {px(kMargin), 0, windowWidth_ - px(kMargin) - px(kCloseSize + 8.0f),
                            bodyTop_};
    canvas_.DrawText(L"设置", titleRect, px(19.0f), RGB(26, 30, 36), 255,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (hover_.field == Field::Close) {
        canvas_.FillRoundRect(closeRect_, px(8.0f), kCloseHover, 255);
    }
    canvas_.DrawText(L"×", closeRect_, px(19.0f), RGB(110, 116, 126), 255);

    // 内容区（滚动裁剪）
    canvas_.SetClipRect({0, bodyTop_, windowWidth_, bodyBottom_});

    const int cardLeft = px(kMargin);
    const int cardRight = windowWidth_ - px(kMargin);
    const int innerLeft = cardLeft + px(kCardPaddingX);
    const int innerRight = cardRight - px(kCardPaddingX);
    const auto rowTop = [&](float cardTop, int rowIndex) {
        return bodyTop_ - scrollY_ + px(cardTop + kFirstRowOffset + rowIndex * kRowPitch);
    };
    // 下拉框：显示当前选中项，展开时高亮
    const auto drawDropdown = [&](Field field) {
        const Choice* choice = FindChoice(field);
        const std::wstring text =
            choice != nullptr && choice->selected < static_cast<int>(choice->items.size())
                ? choice->items[static_cast<size_t>(choice->selected)]
                : std::wstring();
        DrawDropdown(FieldRect(field), text, DropdownExpanded(field), hover_.field == field);
    };

    // 基础
    const int card1Top = bodyTop_ - scrollY_ + px(kCard1Top);
    DrawCard({cardLeft, card1Top, cardRight, card1Top + px(kCard1Height)}, L"基础");
    DrawLabel({innerLeft, rowTop(kCard1Top, 0), innerLeft + px(kLabelWidth),
               rowTop(kCard1Top, 0) + px(kRowHeight)},
              L"功能栏位置");
    drawDropdown(Field::ToolbarPosition);
    DrawLabel({innerLeft, rowTop(kCard1Top, 1), innerLeft + px(kLabelWidth),
               rowTop(kCard1Top, 1) + px(kRowHeight)},
              L"临时文件夹");
    DrawPathField(FieldRect(Field::FolderBrowse), folderPath_,
                  hover_.field == Field::FolderBrowse);
    DrawButton(subRects_[static_cast<int>(Field::FolderBrowse)], L"选择文件夹", false,
               hover_.field == Field::FolderBrowse);
    DrawLabel({innerLeft, rowTop(kCard1Top, 2), innerLeft + px(kLabelWidth),
               rowTop(kCard1Top, 2) + px(kRowHeight)},
              L"保存运行日志");
    DrawSwitch(FieldRect(Field::SaveLog), saveLog_, hover_.field == Field::SaveLog);
    {
        const RECT hint = {FieldRect(Field::SaveLog).right + px(14.0f), rowTop(kCard1Top, 2),
                           innerRight, rowTop(kCard1Top, 2) + px(kRowHeight)};
        canvas_.DrawText(L"关闭后不再写入日志文件", hint, px(13.0f), kHintColor, 255,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // 画面
    const int card2Top = bodyTop_ - scrollY_ + px(kCard2Top);
    DrawCard({cardLeft, card2Top, cardRight, card2Top + px(kCard2Height)}, L"画面");
    DrawLabel({innerLeft, rowTop(kCard2Top, 0), innerLeft + px(kLabelWidth),
               rowTop(kCard2Top, 0) + px(kRowHeight)},
              L"默认摄像头");
    drawDropdown(Field::Camera);
    DrawLabel({innerLeft, rowTop(kCard2Top, 1), innerLeft + px(kLabelWidth),
               rowTop(kCard2Top, 1) + px(kRowHeight)},
              L"摄像头刷新率");
    drawDropdown(Field::Fps);
    DrawLabel({innerLeft, rowTop(kCard2Top, 2), innerLeft + px(kLabelWidth),
               rowTop(kCard2Top, 2) + px(kRowHeight)},
              L"摄像头分辨率");
    drawDropdown(Field::Resolution);
    DrawLabel({innerLeft, rowTop(kCard2Top, 3), innerLeft + px(kLabelWidth),
               rowTop(kCard2Top, 3) + px(kRowHeight)},
              L"自动曝光");
    DrawSwitch(FieldRect(Field::AutoExposure), autoExposure_,
               hover_.field == Field::AutoExposure);

    // 渲染
    const int card3Top = bodyTop_ - scrollY_ + px(kCard3Top);
    DrawCard({cardLeft, card3Top, cardRight, card3Top + px(kCard3Height)}, L"渲染");
    const wchar_t* renderLabels[] = {L"垂直同步", L"抗锯齿（多重采样）", L"双缓冲"};
    const Field renderFields[] = {Field::Vsync, Field::Antialias, Field::DoubleBuffer};
    const bool renderValues[] = {vsync_, antialias_, doubleBuffer_};
    for (int i = 0; i < 3; ++i) {
        const int top = rowTop(kCard3Top, i);
        DrawLabel({innerLeft, top, innerLeft + px(kLabelWidth), top + px(kRowHeight)},
                  renderLabels[i]);
        const RECT rect = FieldRect(renderFields[i]);
        DrawSwitch(rect, renderValues[i], hover_.field == renderFields[i]);
        if (i > 0) {
            const RECT hint = {rect.right + px(14.0f), top, innerRight, top + px(kRowHeight)};
            canvas_.DrawText(L"（需重启程序生效）", hint, px(13.0f), kHintColor, 255,
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    canvas_.ResetClip();

    // 滚动条
    if (maxScroll_ > 0) {
        const int trackTop = bodyTop_ + px(6.0f);
        const int trackBottom = bodyBottom_ - px(6.0f);
        const int trackHeight = trackBottom - trackTop;
        const int thumbHeight =
            std::max(px(40.0f), trackHeight * (trackBottom - trackTop) /
                                    std::max(1, trackBottom - trackTop + maxScroll_));
        const int thumbTop =
            trackTop + (trackHeight - thumbHeight) * scrollY_ / std::max(1, maxScroll_);
        const int barLeft = windowWidth_ - px(10.0f);
        canvas_.FillRoundRect({barLeft, trackTop, barLeft + px(4.0f), trackBottom},
                              px(2.0f), kScrollBar, 180);
        canvas_.FillRoundRect({barLeft, thumbTop, barLeft + px(4.0f), thumbTop + thumbHeight},
                              px(2.0f), RGB(150, 156, 166), 255);
    }

    // 底部按钮
    DrawButton(cancelRect_, L"取消", false, hover_.field == Field::Cancel);
    DrawButton(saveRect_, L"保存", true, hover_.field == Field::Save);

    // 展开的下拉列表绘制在最上层
    if (openField_ != Field::None) {
        DrawDropdownList();
    }
}

} // namespace ui
} // namespace vb
