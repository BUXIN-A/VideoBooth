#pragma once

#include <windows.h>

#include <cstdarg>
#include <string>
#include <vector>

namespace vb {

inline std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                            static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          result.data(), needed);
    return result;
}

inline std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return std::string();
    }
    std::string result(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          result.data(), needed, nullptr, nullptr);
    return result;
}

inline std::string FormatA(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int needed = ::_vscprintf(format, argsCopy);
    va_end(argsCopy);

    std::string result;
    if (needed > 0) {
        std::vector<char> buffer(static_cast<size_t>(needed) + 1, '\0');
        ::vsnprintf_s(buffer.data(), buffer.size(), _TRUNCATE, format, args);
        result.assign(buffer.data());
    }
    va_end(args);
    return result;
}

inline std::wstring FormatW(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int needed = ::_vscwprintf(format, argsCopy);
    va_end(argsCopy);

    std::wstring result;
    if (needed > 0) {
        std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1, L'\0');
        ::vswprintf_s(buffer.data(), buffer.size(), format, args);
        result.assign(buffer.data());
    }
    va_end(args);
    return result;
}

inline std::wstring HresultToWide(HRESULT hr) {
    return FormatW(L"0x%08X", static_cast<unsigned int>(hr));
}

inline bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && ::_wcsicmp(a.c_str(), b.c_str()) == 0;
}

} // namespace vb
