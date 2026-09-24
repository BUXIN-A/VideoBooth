#pragma once

#include <string>

namespace vb {

enum class LogLevel { Debug, Info, Warn, Error };

// 日志输出到 OutputDebugString 与日志文件（UTF-8）。
// enableFile 为 false 时不创建也不写入日志文件（仅输出到调试器）
void LogInit(const std::wstring& filePath, bool enableFile);
void LogShutdown();

// 运行期开关日志文件写入（关闭后只输出到调试器，可随时重新开启）
void LogSetFileEnabled(bool enabled);

void LogWrite(LogLevel level, const char* format, ...);
void LogWriteW(LogLevel level, const wchar_t* format, ...);

} // namespace vb

#define VB_DEBUG(...) ::vb::LogWrite(::vb::LogLevel::Debug, __VA_ARGS__)
#define VB_INFO(...)  ::vb::LogWrite(::vb::LogLevel::Info, __VA_ARGS__)
#define VB_WARN(...)  ::vb::LogWrite(::vb::LogLevel::Warn, __VA_ARGS__)
#define VB_ERROR(...) ::vb::LogWrite(::vb::LogLevel::Error, __VA_ARGS__)
