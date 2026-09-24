#pragma once

namespace vb {
namespace core {

// 基础工具模式
enum class ToolMode {
    Select = 0,   // 选择：画面拖动
    Annotate = 1, // 批注：书写
    Erase = 2,    // 橡皮：擦除笔迹
};

// 视图模式
enum class ViewMode {
    Live = 0,     // 实时画面
    Album = 1,    // 相册查看
    Settings = 2, // 设置
};

// 画面来源
enum class PictureSource {
    None = 0,   // 尚无画面
    Camera = 1, // 摄像头实时画面
    Error = 2,  // 无摄像头可用（error.png）
    Locked = 3, // 锁定画面
    Album = 4,  // 相册中正在查看的照片
};

struct UiState {
    ToolMode tool = ToolMode::Select;
    ViewMode view = ViewMode::Live;
    bool locked = false;
    bool cameraAvailable = false;
    PictureSource picture = PictureSource::None;

    // 画面变换
    float zoom = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    int rotationQuarter = 0; // 顺时针 90° 次数（0-3）

    // 画面可缩放/拖动（无摄像头占位图不可操作，相册照片可以）
    bool pictureZoomable() const { return picture != PictureSource::Error; }
    // 画面可批注/擦除（无摄像头占位图不可；相册照片可以，笔迹按图片单独存放）
    bool pictureAnnotatable() const { return picture != PictureSource::Error; }
    // 画面可拍照/锁定（相册照片不参与采集，不可拍照）
    bool pictureShootable() const { return picture == PictureSource::Camera || picture == PictureSource::Locked; }
};

} // namespace core
} // namespace vb
