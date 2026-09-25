#include "util/Log.h"

#include "util/Strings.h"

#include <windows.h>

#include <clocale>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace vb {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::wstring g_filePath;
bool g_fileEnabled = false;

// 以追加方式打开日志文件（新文件写入 UTF-8 BOM）
void OpenLogFileLocked() {
    if (g_file != nullptr || !g_fileEnabled || g_filePath.empty()) {
        return;
    }
    FILE* file = nullptr;
    if (::_wfopen_s(&file, g_filePath.c_str(), L"ab") == 0 && file != nullptr) {
        g_file = file;
        if (std::ftell(g_file) == 0) {
            const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
            std::fwrite(bom, 1, sizeof(bom), g_file);
            std::fflush(g_file);
        }
    }
}

const char* LevelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "INFO ";
}

void WriteLine(LogLevel level, const std::wstring& text) {
    SYSTEMTIME st = {};
    ::GetLocalTime(&st);

    wchar_t head[64] = {};
    ::swprintf_s(head, L"[%02u:%02u:%02u.%03u][%hs] ",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, LevelTag(level));

    std::wstring line;
    line.reserve(64 + text.size());
    line.append(head);
    line.append(text);
    line.append(L"\r\n");
    ::OutputDebugStringW(line.c_str());

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file == nullptr) {
        return;
    }
    const std::string utf8 = WideToUtf8(line);
    if (!utf8.empty()) {
        std::fwrite(utf8.data(), 1, utf8.size(), g_file);
        // 仅错误级立即落盘：逐行同步会明显拖慢密集的调试日志
        if (level == LogLevel::Error) {
            std::fflush(g_file);
        }
    }
}

} // namespace

void LogInit(const std::wstring& filePath, bool enableFile) {
    // 使用 UTF-8 区域设置，保证窄字符格式化函数（vsnprintf 的 %ls 转换）
    // 能正确输出中文等非 ASCII 内容
    ::setlocale(LC_ALL, ".UTF-8");

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != nullptr) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    g_filePath = filePath;
    g_fileEnabled = enableFile;
    OpenLogFileLocked();
}

void LogSetFileEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fileEnabled = enabled;
    if (!enabled) {
        if (g_file != nullptr) {
            std::fclose(g_file);
            g_file = nullptr;
        }
        return;
    }
    OpenLogFileLocked();
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != nullptr) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void LogWrite(LogLevel level, const char* format, ...) {
    if (format == nullptr) {
        return;
    }
    va_list args;
    va_start(args, format);
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int needed = ::_vscprintf(format, argsCopy);
    va_end(argsCopy);

    // 常见日志在栈上格式化，仅在超长时回退到堆缓冲
    char stack[1024] = {};
    std::vector<char> heap;
    char* buffer = stack;
    size_t capacity = sizeof(stack);
    if (needed > 0 && static_cast<size_t>(needed) + 1 > capacity) {
        heap.resize(static_cast<size_t>(needed) + 1);
        buffer = heap.data();
        capacity = heap.size();
    }
    if (needed > 0) {
        ::vsnprintf_s(buffer, capacity, _TRUNCATE, format, args);
    }
    va_end(args);

    WriteLine(level, Utf8ToWide(std::string(buffer)));
}

void LogWriteW(LogLevel level, const wchar_t* format, ...) {
    if (format == nullptr) {
        return;
    }
    va_list args;
    va_start(args, format);
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int needed = ::_vscwprintf(format, argsCopy);
    va_end(argsCopy);

    // 常见日志在栈上格式化，仅在超长时回退到堆缓冲
    wchar_t stack[1024] = {};
    std::vector<wchar_t> heap;
    wchar_t* buffer = stack;
    size_t capacity = sizeof(stack) / sizeof(stack[0]);
    if (needed > 0 && static_cast<size_t>(needed) + 1 > capacity) {
        heap.resize(static_cast<size_t>(needed) + 1);
        buffer = heap.data();
        capacity = heap.size();
    }
    if (needed > 0) {
        ::vswprintf_s(buffer, capacity, format, args);
    }
    va_end(args);

    WriteLine(level, std::wstring(buffer));
}

} // namespace vb
