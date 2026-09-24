#pragma once

#include <windows.h>

#include <vector>

#include "capture/Camera.h"

namespace vb {
namespace ui {

// 摄像头选择对话框：配置文件中的默认摄像头不可用时，由用户从设备列表中选择
class CameraSelectDialog {
public:
    // 返回所选设备在 devices 中的下标；用户取消时返回 -1
    static int Run(HINSTANCE instance, const std::vector<capture::CameraInfo>& devices);

private:
    static bool EnsureClassRegistered(HINSTANCE instance);
    bool Create(HINSTANCE instance, const std::vector<capture::CameraInfo>& devices);
    int RunModal();
    void Destroy();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnCreate();
    void ApplyFont(HWND control);
    void Confirm(int index);
    void Cancel();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND label_ = nullptr;
    HWND list_ = nullptr;
    HWND confirm_ = nullptr;
    HWND cancel_ = nullptr;
    HFONT font_ = nullptr;
    std::vector<capture::CameraInfo> devices_;
    int result_ = -1;
    bool running_ = true;
};

} // namespace ui
} // namespace vb
