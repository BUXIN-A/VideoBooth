#pragma once

#include <windows.h>

#include <string>

namespace vb {
namespace paths {

// 程序所在目录（不含结尾反斜杠）
std::wstring ExeDir();
std::wstring AssetsDir();
std::wstring AssetPath(const wchar_t* fileName);

std::wstring ConfigPath();
std::wstring ConfigBackupPath();
std::wstring LogPath();

// %TEMP%\VideoBooth\Photos
std::wstring DefaultPhotoDir();

// 当前用户的桌面文件夹（文件对话框的默认位置），获取失败时回退为用户目录
std::wstring DesktopDir();

std::wstring JoinPath(const std::wstring& parent, const std::wstring& child);
bool FileExists(const std::wstring& path);
bool EnsureDirectory(const std::wstring& path);

// 临时照片文件名：IMG_yyyyMMdd_HHmmss_xxx.jpg
std::wstring PhotoFileName(const SYSTEMTIME& time);

} // namespace paths
} // namespace vb
