#include "ui/AlbumPanel.h"

#include "util/Log.h"
#include "util/Strings.h"

#include <algorithm>
#include <cstdlib>

namespace vb {
namespace ui {
namespace {

// 逻辑尺寸（按 UI 比例缩放）
constexpr float kPadding = 18.0f;
constexpr float kTitleHeight = 22.0f;
constexpr float kTitleGap = 12.0f;
constexpr float kCardWidth = 168.0f;
constexpr float kCardInsetX = 6.0f;
constexpr float kCardInsetTop = 6.0f;
constexpr float kCardInsetBottom = 4.0f;
constexpr float kThumbHeight = 96.0f;
constexpr float kThumbToButtons = 8.0f;
constexpr float kButtonSize = 30.0f;
constexpr float kButtonGap = 8.0f;
constexpr float kCardGap = 14.0f;
constexpr float kBarGap = 4.0f;
constexpr float kBarHeight = 4.0f;
// 标题行按钮：import_picture.png / save_picture.png 为 240x60 的长条图片，按原比例绘制
constexpr float kHeaderButtonHeight = 24.0f;
constexpr float kHeaderButtonAspect = 4.0f;
constexpr float kHeaderButtonGap = 6.0f;
constexpr float kHeaderButtonInset = 12.0f;
constexpr float kHeaderLabelFont = 13.0f;
constexpr float kHeaderLabelGap = 6.0f;
constexpr float kSideMargin = 60.0f;
constexpr float kMinPanelWidth = 420.0f;
// 面板宽度固定为窗口宽度的比例，避免随照片数量改变面板大小
constexpr float kPanelWidthRatio = 0.6f;
constexpr float kScreenMargin = 12.0f;
// 卡片高度：缩略图 + 按钮行 + 上下内边距
constexpr float kCardHeight =
    kCardInsetTop + kThumbHeight + kThumbToButtons + kButtonSize + kCardInsetBottom;

constexpr int kButtonCount = 3;
const wchar_t* const kButtonIcons[kButtonCount] = {L"delete.png", L"save.png", L"show.png"};

// 标题行“合成笔迹”选择框
const wchar_t* const kComposeLabel = L"合成笔迹";
const wchar_t* const kComposeIconOff = L"checkbox.png";
const wchar_t* const kComposeIconOn = L"checkbox_ok.png";

constexpr COLORREF kPanelColor = RGB(238, 238, 238);
constexpr COLORREF kCardColor = RGB(255, 255, 255);
constexpr COLORREF kThumbBack = RGB(226, 226, 226);
constexpr COLORREF kButtonColor = RGB(234, 234, 234);
constexpr COLORREF kButtonHover = RGB(206, 220, 242);
constexpr COLORREF kAccentColor = RGB(45, 127, 249);
constexpr COLORREF kTitleColor = RGB(70, 70, 70);
constexpr COLORREF kSubTitleColor = RGB(130, 130, 130);
constexpr COLORREF kBarTrack = RGB(214, 214, 214);
constexpr COLORREF kBarThumb = RGB(150, 150, 150);

bool InsideRect(int x, int y, const RECT& rect) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

bool RectsOverlap(const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

} // namespace

void AlbumPanel::Init(Resources* resources) {
    resources_ = resources;
}

void AlbumPanel::SetScale(float uiScale) {
    scale_ = std::max(0.5f, uiScale);
}

void AlbumPanel::SetOpen(bool open) {
    if (open_ == open) {
        return;
    }
    open_ = open;
    if (!open_) {
        dragging_ = false;
        dragged_ = false;
        hover_ = AlbumPanelHit();
    }
}

void AlbumPanel::SetPhotoCount(size_t count) {
    photoCount_ = count;
    if (shownIndex_ >= static_cast<long long>(count)) {
        shownIndex_ = -1;
    }
    scrollX_ = std::max(0, std::min(scrollX_, MaxScroll()));
}

void AlbumPanel::SetHover(const AlbumPanelHit& hit) {
    hover_ = hit;
}

int AlbumPanel::PanelHeight() const {
    const float total = kPadding + kTitleHeight + kTitleGap + kCardHeight + kBarGap + kBarHeight +
                        kPadding;
    return static_cast<int>(total * scale_);
}

RECT AlbumPanel::ViewportRect() const {
    const float s = scale_;
    const int padding = static_cast<int>(kPadding * s);
    const int top = padding + static_cast<int>((kTitleHeight + kTitleGap) * s);
    const int cardHeight = static_cast<int>(kCardHeight * s);
    const int width = std::max(1, static_cast<int>(panelRect_.right - panelRect_.left) - padding * 2);
    return {padding, top, padding + width, top + cardHeight};
}

void AlbumPanel::UpdateHeaderButtons() {
    const float s = scale_;
    const RECT viewport = ViewportRect();
    const int height = static_cast<int>(kHeaderButtonHeight * s);
    const int width = static_cast<int>(kHeaderButtonHeight * kHeaderButtonAspect * s);
    const int gap = static_cast<int>(kHeaderButtonGap * s);
    const int titleTop = static_cast<int>(kPadding * s);
    const int titleHeight = static_cast<int>(kTitleHeight * s);
    const int top = titleTop + (titleHeight - height) / 2;
    headerSaveAllRect_ = {viewport.right - width, top, viewport.right, top + height};
    headerImportRect_ = {headerSaveAllRect_.left - gap - width, top,
                         headerSaveAllRect_.left - gap, top + height};

    // “合成笔迹”选择框位于导入按钮左侧（图标 + 文字整体可点击）
    const int textWidth =
        canvas_.MeasureText(kComposeLabel, static_cast<int>(kHeaderLabelFont * s)).cx;
    const int composeWidth = height + static_cast<int>(kHeaderLabelGap * s) + textWidth;
    headerComposeRect_ = {headerImportRect_.left - gap - composeWidth, top,
                          headerImportRect_.left - gap, top + height};
    headerComposeBoxRect_ = {headerComposeRect_.left, top, headerComposeRect_.left + height,
                             top + height};
}

int AlbumPanel::ContentWidth() const {
    if (photoCount_ == 0) {
        return 0;
    }
    const int cardWidth = static_cast<int>(kCardWidth * scale_);
    const int cardGap = static_cast<int>(kCardGap * scale_);
    return static_cast<int>(photoCount_) * cardWidth +
           static_cast<int>(photoCount_ - 1) * cardGap;
}

int AlbumPanel::MaxScroll() const {
    if (photoCount_ == 0) {
        return 0;
    }
    const RECT viewport = ViewportRect();
    const int viewportWidth = static_cast<int>(viewport.right - viewport.left);
    return std::max(0, ContentWidth() - viewportWidth);
}

void AlbumPanel::Layout(const RECT& toolbarBounds, int clientWidth, int clientHeight) {
    const float s = scale_;
    const int minPanelWidth = static_cast<int>(kMinPanelWidth * s);

    // 面板尺寸固定：宽度取窗口宽度的固定比例（不随照片数量自适应），高度由内容布局决定
    const int maxPanelWidth =
        std::max(minPanelWidth, clientWidth - static_cast<int>(kSideMargin * s) * 2);
    int panelWidth = static_cast<int>(static_cast<float>(clientWidth) * kPanelWidthRatio);
    panelWidth = std::max(minPanelWidth, std::min(panelWidth, maxPanelWidth));
    const int panelHeight = PanelHeight();

    const bool vertical = (toolbarBounds.right - toolbarBounds.left) <
                          (toolbarBounds.bottom - toolbarBounds.top);
    int left = 0;
    int top = 0;
    if (vertical) {
        // 纵向功能栏：面板贴在功能栏左侧并垂直居中
        left = toolbarBounds.left - static_cast<int>(kScreenMargin * s) - panelWidth;
        top = (clientHeight - panelHeight) / 2;
    } else {
        // 横向功能栏：面板居中贴在功能栏上方
        left = (clientWidth - panelWidth) / 2;
        top = toolbarBounds.top - panelHeight;
    }
    const int margin = static_cast<int>(kScreenMargin * s);
    left = std::max(margin, std::min(left, clientWidth - panelWidth - margin));
    top = std::max(margin, std::min(top, clientHeight - panelHeight - margin));
    panelRect_ = {left, top, left + panelWidth, top + panelHeight};

    UpdateHeaderButtons();
    scrollX_ = std::max(0, std::min(scrollX_, MaxScroll()));
}

bool AlbumPanel::ContainsPoint(POINT point) const {
    if (!open_) {
        return false;
    }
    return point.x >= panelRect_.left && point.x < panelRect_.right && point.y >= panelRect_.top &&
           point.y < panelRect_.bottom;
}

void AlbumPanel::CardGeometryAt(size_t index, CardGeometry& geometry) const {
    const float s = scale_;
    const RECT viewport = ViewportRect();
    const int cardWidth = static_cast<int>(kCardWidth * s);
    const int cardGap = static_cast<int>(kCardGap * s);
    const int viewportWidth = viewport.right - viewport.left;
    // 内容不足一屏时居中显示
    const int offset = std::max(0, (viewportWidth - ContentWidth()) / 2);

    const int left = viewport.left + offset - scrollX_ +
                     static_cast<int>(index) * (cardWidth + cardGap);
    const int top = viewport.top;
    const int cardHeight = static_cast<int>(kCardHeight * s);
    geometry.card = {left, top, left + cardWidth, top + cardHeight};

    const int insetX = static_cast<int>(kCardInsetX * s);
    const int insetTop = static_cast<int>(kCardInsetTop * s);
    const int thumbHeight = static_cast<int>(kThumbHeight * s);
    geometry.thumb = {left + insetX, top + insetTop, left + cardWidth - insetX,
                      top + insetTop + thumbHeight};

    const int buttonSize = static_cast<int>(kButtonSize * s);
    const int buttonGap = static_cast<int>(kButtonGap * s);
    const int buttonsWidth = kButtonCount * buttonSize + (kButtonCount - 1) * buttonGap;
    const int buttonsTop =
        top + insetTop + thumbHeight + static_cast<int>(kThumbToButtons * s);
    int buttonLeft = left + (cardWidth - buttonsWidth) / 2;
    for (int i = 0; i < kButtonCount; ++i) {
        geometry.buttons[i] = {buttonLeft, buttonsTop, buttonLeft + buttonSize,
                               buttonsTop + buttonSize};
        buttonLeft += buttonSize + buttonGap;
    }
}

AlbumPanelHit AlbumPanel::HitTest(POINT point) const {
    AlbumPanelHit hit;
    if (!open_ || !ContainsPoint(point)) {
        return hit;
    }
    const int localX = point.x - panelRect_.left;
    const int localY = point.y - panelRect_.top;
    // 标题行按钮：无照片时同样可用（导入照片）
    if (InsideRect(localX, localY, headerSaveAllRect_)) {
        hit.kind = AlbumPanelHit::Kind::SaveAll;
        return hit;
    }
    if (InsideRect(localX, localY, headerImportRect_)) {
        hit.kind = AlbumPanelHit::Kind::Import;
        return hit;
    }
    if (InsideRect(localX, localY, headerComposeRect_)) {
        hit.kind = AlbumPanelHit::Kind::ComposeAnnotation;
        return hit;
    }
    if (photoCount_ == 0) {
        return hit;
    }
    // 仅滚动视口内的内容可命中，避免命中被裁掉的卡片
    if (!InsideRect(localX, localY, ViewportRect())) {
        return hit;
    }

    for (size_t i = 0; i < photoCount_; ++i) {
        CardGeometry geometry;
        CardGeometryAt(i, geometry);
        if (geometry.card.left > localX) {
            break; // 卡片按横坐标递增排列，后续卡片只会更靠右
        }
        if (!InsideRect(localX, localY, geometry.card)) {
            continue;
        }
        for (int b = 0; b < kButtonCount; ++b) {
            if (InsideRect(localX, localY, geometry.buttons[b])) {
                hit.kind = b == 0 ? AlbumPanelHit::Kind::Delete
                                  : (b == 1 ? AlbumPanelHit::Kind::Save
                                            : AlbumPanelHit::Kind::Show);
                hit.index = i;
                return hit;
            }
        }
        if (InsideRect(localX, localY, geometry.thumb)) {
            hit.kind = AlbumPanelHit::Kind::Thumbnail;
            hit.index = i;
            return hit;
        }
    }
    return hit;
}

void AlbumPanel::ScrollBy(int deltaPixels) {
    scrollX_ = std::max(0, std::min(scrollX_ + deltaPixels, MaxScroll()));
}

void AlbumPanel::BeginDrag(int x) {
    dragging_ = true;
    dragged_ = false;
    dragOriginX_ = x;
    dragStartScroll_ = scrollX_;
}

void AlbumPanel::DragTo(int x) {
    if (!dragging_) {
        return;
    }
    const int delta = x - dragOriginX_;
    if (std::abs(delta) > 4) {
        dragged_ = true;
    }
    scrollX_ = std::max(0, std::min(dragStartScroll_ - delta, MaxScroll()));
}

void AlbumPanel::EndDrag() {
    dragging_ = false;
}

void AlbumPanel::RenderCard(const CardGeometry& geometry, size_t index,
                            const ThumbnailProvider& provider,
                            const img::Image* const* buttonIcons) {
    const float s = scale_;
    const bool shown = static_cast<long long>(index) == shownIndex_;
    const bool thumbHover = hover_.kind == AlbumPanelHit::Kind::Thumbnail && hover_.index == index;

    if (shown) {
        RECT ring = geometry.card;
        ::InflateRect(&ring, static_cast<int>(3.0f * s), static_cast<int>(3.0f * s));
        canvas_.FillRoundRect(ring, static_cast<int>(12.0f * s), kAccentColor, 255);
    }
    canvas_.FillRoundRect(geometry.card, static_cast<int>(10.0f * s), kCardColor, 255);

    // 缩略图：等比适配到缩略图框内，居中留边
    canvas_.FillRect(geometry.thumb, kThumbBack, 255);
    const img::Image* thumbnail = provider ? provider(index) : nullptr;
    if (thumbnail != nullptr && thumbnail->Valid()) {
        const int boxWidth = geometry.thumb.right - geometry.thumb.left;
        const int boxHeight = geometry.thumb.bottom - geometry.thumb.top;
        const float fit = std::min(static_cast<float>(boxWidth) / thumbnail->width(),
                                   static_cast<float>(boxHeight) / thumbnail->height());
        const int drawWidth = std::max(1, static_cast<int>(thumbnail->width() * fit));
        const int drawHeight = std::max(1, static_cast<int>(thumbnail->height() * fit));
        const int drawLeft = geometry.thumb.left + (boxWidth - drawWidth) / 2;
        const int drawTop = geometry.thumb.top + (boxHeight - drawHeight) / 2;
        const RECT dest = {drawLeft, drawTop, drawLeft + drawWidth, drawTop + drawHeight};
        canvas_.DrawPixels(thumbnail->pixels(), thumbnail->width(), thumbnail->height(),
                           thumbnail->stride(), dest);
    } else {
        canvas_.DrawText(L"…", geometry.thumb, static_cast<int>(20.0f * s), kSubTitleColor, 200);
    }
    if (thumbHover && !shown) {
        canvas_.DrawDashedRect(geometry.thumb, kAccentColor, 220, std::max(1, static_cast<int>(2.0f * s)),
                               static_cast<int>(10.0f * s), static_cast<int>(6.0f * s));
    }

    // 三个操作按钮
    const int iconSize = static_cast<int>(18.0f * s);
    for (int b = 0; b < kButtonCount; ++b) {
        const RECT& button = geometry.buttons[b];
        const AlbumPanelHit::Kind kind =
            b == 0 ? AlbumPanelHit::Kind::Delete
                   : (b == 1 ? AlbumPanelHit::Kind::Save : AlbumPanelHit::Kind::Show);
        const bool hovered = hover_.kind == kind && hover_.index == index;
        const bool accent = kind == AlbumPanelHit::Kind::Show && shown;
        const COLORREF background = accent ? kAccentColor : (hovered ? kButtonHover : kButtonColor);
        canvas_.FillRoundRect(button, static_cast<int>(8.0f * s), background, 255);

        const img::Image* icon = buttonIcons[b];
        if (icon != nullptr && icon->Valid()) {
            const int centerX = (button.left + button.right) / 2;
            const int centerY = (button.top + button.bottom) / 2;
            const RECT dest = {centerX - iconSize / 2, centerY - iconSize / 2,
                               centerX - iconSize / 2 + iconSize,
                               centerY - iconSize / 2 + iconSize};
            canvas_.DrawPixels(icon->pixels(), icon->width(), icon->height(), icon->stride(),
                               dest);
        } else {
            static const wchar_t* const kFallback[kButtonCount] = {L"删除", L"保存", L"展示"};
            canvas_.DrawText(kFallback[b], button, static_cast<int>(11.0f * s),
                             accent ? RGB(255, 255, 255) : kTitleColor, 255);
        }
    }
}

void AlbumPanel::RenderHeaderButton(const RECT& rect, const wchar_t* icon, const wchar_t* fallback,
                                    bool hovered) {
    const img::Image* image = resources_ != nullptr ? resources_->Get(icon) : nullptr;
    if (image != nullptr && image->Valid()) {
        canvas_.DrawPixels(image->pixels(), image->width(), image->height(), image->stride(), rect);
        if (hovered) {
            // 图标自带底色，悬停时整体提亮以便区分
            canvas_.FillRoundRect(rect, static_cast<int>(7.0f * scale_), RGB(255, 255, 255), 40);
        }
        return;
    }
    canvas_.FillRoundRect(rect, static_cast<int>(7.0f * scale_), hovered ? kButtonHover : kButtonColor,
                          255);
    canvas_.DrawText(fallback, rect, static_cast<int>(12.0f * scale_), kTitleColor, 255);
}

void AlbumPanel::RenderHeaderCheckBox(bool hovered) {
    const float s = scale_;
    const RECT& box = headerComposeBoxRect_;
    if (hovered) {
        RECT row = headerComposeRect_;
        ::InflateRect(&row, static_cast<int>(4.0f * s), static_cast<int>(2.0f * s));
        canvas_.FillRoundRect(row, static_cast<int>(6.0f * s), kButtonHover, 200);
    }
    const img::Image* icon = resources_ != nullptr
                                 ? resources_->Get(composeAnnotation_ ? kComposeIconOn
                                                                     : kComposeIconOff)
                                 : nullptr;
    if (icon != nullptr && icon->Valid()) {
        canvas_.DrawPixels(icon->pixels(), icon->width(), icon->height(), icon->stride(), box);
    } else {
        canvas_.FillRoundRect(box, static_cast<int>(4.0f * s),
                              composeAnnotation_ ? kAccentColor : kButtonColor, 255);
    }
    const RECT textRect = {box.right + static_cast<int>(kHeaderLabelGap * s),
                           headerComposeRect_.top, headerComposeRect_.right,
                           headerComposeRect_.bottom};
    canvas_.DrawText(kComposeLabel, textRect, static_cast<int>(kHeaderLabelFont * s),
                     composeAnnotation_ ? kAccentColor : kTitleColor, 255,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void AlbumPanel::RenderScrollBar() {
    const int maxScroll = MaxScroll();
    if (maxScroll <= 0) {
        return;
    }
    const float s = scale_;
    const RECT viewport = ViewportRect();
    const int trackTop = viewport.bottom + static_cast<int>(kBarGap * s);
    const RECT track = {viewport.left, trackTop, viewport.right,
                        trackTop + static_cast<int>(kBarHeight * s)};
    canvas_.FillRoundRect(track, static_cast<int>(kBarHeight * s / 2), kBarTrack, 255);

    const int trackWidth = track.right - track.left;
    if (trackWidth <= 0) {
        return;
    }
    const int contentWidth = trackWidth + maxScroll;
    const int thumbWidth =
        std::max(static_cast<int>(40.0f * s), trackWidth * trackWidth / std::max(1, contentWidth));
    const int thumbLeft = track.left + (trackWidth - thumbWidth) * scrollX_ / maxScroll;
    const RECT thumb = {thumbLeft, track.top, thumbLeft + thumbWidth, track.bottom};
    canvas_.FillRoundRect(thumb, static_cast<int>(kBarHeight * s / 2), kBarThumb, 255);
}

void AlbumPanel::Render(const ThumbnailProvider& provider) {
    if (!open_) {
        return;
    }
    const int width = static_cast<int>(panelRect_.right - panelRect_.left);
    const int height = static_cast<int>(panelRect_.bottom - panelRect_.top);
    if (width <= 0 || height <= 0) {
        return;
    }
    if (!canvas_.Resize(width, height)) {
        return;
    }
    canvas_.Clear();

    const float s = scale_;
    canvas_.FillRoundRect({0, 0, width, height}, static_cast<int>(16.0f * s), kPanelColor, 238);

    const RECT viewport = ViewportRect();
    const int titleHeight = static_cast<int>(kTitleHeight * s);
    const RECT titleRect = {viewport.left, static_cast<int>(kPadding * s), viewport.right,
                            static_cast<int>(kPadding * s) + titleHeight};
    canvas_.DrawText(L"相册", titleRect, static_cast<int>(15.0f * s), kTitleColor, 255,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 标题行右侧：合成笔迹选择框、导入照片、保存照片
    RenderHeaderCheckBox(hover_.kind == AlbumPanelHit::Kind::ComposeAnnotation);
    RenderHeaderButton(headerImportRect_, L"import_picture.png", L"导入",
                       hover_.kind == AlbumPanelHit::Kind::Import);
    RenderHeaderButton(headerSaveAllRect_, L"save_picture.png", L"保存",
                       hover_.kind == AlbumPanelHit::Kind::SaveAll);

    if (photoCount_ > 0) {
        const RECT countRect = {viewport.left, titleRect.top,
                                headerComposeRect_.left - static_cast<int>(kHeaderButtonInset * s),
                                titleRect.bottom};
        canvas_.DrawText(
            FormatW(L"共 %llu 张", static_cast<unsigned long long>(photoCount_)), countRect,
            static_cast<int>(13.0f * s), kSubTitleColor, 255,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (photoCount_ == 0) {
        canvas_.DrawText(L"暂无照片，拍照或导入照片后在此查看", viewport,
                         static_cast<int>(15.0f * s), kSubTitleColor, 255);
        return;
    }

    // 滚动视口内裁剪绘制，保证滚出面板的卡片不会溢出
    canvas_.SetClipRect(viewport);
    // 按钮图标在循环外一次取出，避免每张卡片重复查找资源
    const img::Image* buttonIcons[kButtonCount] = {};
    for (int b = 0; b < kButtonCount; ++b) {
        buttonIcons[b] = resources_ != nullptr ? resources_->Get(kButtonIcons[b]) : nullptr;
    }
    for (size_t i = 0; i < photoCount_; ++i) {
        CardGeometry geometry;
        CardGeometryAt(i, geometry);
        if (geometry.card.left >= viewport.right) {
            break; // 卡片按横坐标递增排列，越过视口右边界后都不可见
        }
        if (!RectsOverlap(geometry.card, viewport)) {
            continue;
        }
        RenderCard(geometry, i, provider, buttonIcons);
    }
    canvas_.ResetClip();

    RenderScrollBar();
}

} // namespace ui
} // namespace vb
