#pragma once

#include <windows.h>

#include <cstddef>
#include <functional>

#include "ui/OverlayCanvas.h"
#include "ui/Resources.h"

namespace vb {
namespace ui {

struct AlbumPanelHit {
    enum class Kind { None, Thumbnail, Delete, Save, Show, SaveAll, Import, ComposeAnnotation };
    Kind kind = Kind::None;
    size_t index = 0;
};

// 相册浮层：贴在功能栏上方，横向排列照片卡片（缩略图 + 删除/保存/展示三个按钮），
// 支持鼠标滚轮横向滚动与拖动滚动。
class AlbumPanel {
public:
    // 缩略图提供者：按索引返回缩略图，未就绪时返回 nullptr
    using ThumbnailProvider = std::function<const img::Image*(size_t index)>;

    void Init(Resources* resources);
    void SetScale(float uiScale);

    // 依据功能栏位置与窗口尺寸重算面板几何（照片数量会影响面板宽度）
    void Layout(const RECT& toolbarBounds, int clientWidth, int clientHeight);

    void SetOpen(bool open);
    bool isOpen() const { return open_; }

    void SetPhotoCount(size_t count);

    // 当前正在画面框中展示的照片下标，-1 表示未展示
    void SetShownIndex(long long index) { shownIndex_ = index; }

    // 导出时是否把批注笔迹合成进照片（面板上的选择框）
    void SetComposeAnnotation(bool value) { composeAnnotation_ = value; }
    bool composeAnnotation() const { return composeAnnotation_; }

    void SetHover(const AlbumPanelHit& hit);
    const AlbumPanelHit& hover() const { return hover_; }

    RECT bounds() const { return panelRect_; }
    bool ContainsPoint(POINT point) const;

    // 滚动：滚轮按档滚动，拖动按像素滚动
    void ScrollBy(int deltaPixels);
    void ResetScroll() { scrollX_ = 0; }
    void BeginDrag(int x);
    void DragTo(int x);
    void EndDrag();
    bool IsDragging() const { return dragging_; }
    bool WasDragged() const { return dragged_; }

    AlbumPanelHit HitTest(POINT point) const;

    void Render(const ThumbnailProvider& provider);
    const OverlayCanvas& canvas() const { return canvas_; }

private:
    struct CardGeometry {
        RECT card = {0, 0, 0, 0};
        RECT thumb = {0, 0, 0, 0};
        RECT buttons[3] = {};
    };

    int PanelHeight() const;
    int MaxScroll() const;
    RECT ViewportRect() const;
    // 全部卡片横向排列后的总宽度
    int ContentWidth() const;
    // 标题行右侧的“导入照片”“保存照片”按钮
    void UpdateHeaderButtons();
    void CardGeometryAt(size_t index, CardGeometry& geometry) const;
    void RenderCard(const CardGeometry& geometry, size_t index, const ThumbnailProvider& provider,
                    const img::Image* const* buttonIcons);
    void RenderScrollBar();
    void RenderHeaderButton(const RECT& rect, const wchar_t* icon, const wchar_t* fallback,
                            bool hovered);
    // 标题行右侧的“合成笔迹”选择框
    void RenderHeaderCheckBox(bool hovered);

    Resources* resources_ = nullptr;
    float scale_ = 1.0f;
    bool open_ = false;

    RECT panelRect_ = {0, 0, 0, 0};
    RECT headerImportRect_ = {0, 0, 0, 0};
    RECT headerSaveAllRect_ = {0, 0, 0, 0};
    RECT headerComposeRect_ = {0, 0, 0, 0};     // “合成笔迹”整行点击区（图标 + 文字）
    RECT headerComposeBoxRect_ = {0, 0, 0, 0};  // 选择框图标区域

    size_t photoCount_ = 0;
    long long shownIndex_ = -1;
    bool composeAnnotation_ = true; // 默认把笔迹合成进导出的照片
    AlbumPanelHit hover_;

    int scrollX_ = 0;
    bool dragging_ = false;
    bool dragged_ = false;
    int dragOriginX_ = 0;
    int dragStartScroll_ = 0;

    OverlayCanvas canvas_;
};

} // namespace ui
} // namespace vb
