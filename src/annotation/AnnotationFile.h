#pragma once

#include <string>

#include "annotation/StrokeLayer.h"

namespace vb {
namespace annotation {

// 每张图片的笔迹单独存放为临时文件：文件名 = 图片文件名 + ".ann.png"。
// 实时画面（相机/锁定帧）没有对应照片，使用固定的 kVliveAnnotationName，
// 这样在照片与实时画面之间来回切换时两边的笔迹都不会丢失。
inline constexpr wchar_t kLiveAnnotationKey[] = L"_live";

// 由画面标识（照片文件名，实时画面传空字符串）得到笔迹文件完整路径
std::wstring AnnotationPathFor(const std::wstring& directory, const std::wstring& pictureKey);

// 保存笔迹：层为空（无笔迹）时删除同名文件
bool SaveToFile(const std::wstring& path, const StrokeLayer& layer);
// 加载笔迹：文件尺寸与期望尺寸不一致（或文件不存在）时返回 false，且不修改 layer
bool LoadFromFile(const std::wstring& path, StrokeLayer& layer, int expectedWidth,
                  int expectedHeight);
// 删除笔迹文件（不存在时也返回成功）
bool RemoveFile(const std::wstring& path);
// 把笔迹文件合成到目标图像像素（目标为 32 位预乘 BGRA）
bool CompositeFileInto(const std::wstring& path, uint8_t* target, int width, int height,
                       int stride);

} // namespace annotation
} // namespace vb
