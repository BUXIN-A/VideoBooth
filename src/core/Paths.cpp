#include "core/Paths.h"

#include <shlobj.h>

#include <vector>

namespace vb {
namespace paths {
namespace {

std::wstring TrimTrailingSlash(std::wstring path) {
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

} // namespace

std::wstring ExeDir() {
    static const std::wstring cached = [] {
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;) {
            const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(),
                                                      static_cast<DWORD>(buffer.size()));
            if (length == 0) {
                return std::wstring();
            }
            if (length < buffer.size()) {
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
        std::wstring full(buffer.data());
        const size_t pos = full.find_last_of(L"\\/");
        if (pos == std::wstring::npos) {
            return std::wstring();
        }
        return full.substr(0, pos);
    }();
    return cached;
}

std::wstring AssetsDir() {
    return JoinPath(ExeDir(), L"assets");
}

std::wstring AssetPath(const wchar_t* fileName) {
    return JoinPath(AssetsDir(), fileName != nullptr ? fileName : L"");
}

std::wstring ConfigPath() {
    return JoinPath(ExeDir(), L"config.json");
}

std::wstring ConfigBackupPath() {
    return JoinPath(ExeDir(), L"config.bak.json");
}

std::wstring LogPath() {
    return JoinPath(ExeDir(), L"VideoBooth.log");
}

std::wstring DefaultPhotoDir() {
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD length = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (length == 0 || length >= buffer.size()) {
        return JoinPath(ExeDir(), L"Photos");
    }
    std::wstring temp(buffer.data(), length);
    return JoinPath(JoinPath(TrimTrailingSlash(temp), L"VideoBooth"), L"Photos");
}

std::wstring DesktopDir() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &raw)) &&
        raw != nullptr) {
        std::wstring desktop(raw);
        ::CoTaskMemFree(raw);
        if (!desktop.empty()) {
            return desktop;
        }
    }
    // 回退：用户目录（Windows 桌面位于用户目录下，无桌面文件夹时仍可定位）
    PWSTR profile = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &profile)) &&
        profile != nullptr) {
        std::wstring home(profile);
        ::CoTaskMemFree(profile);
        return home;
    }
    return ExeDir();
}

std::wstring JoinPath(const std::wstring& parent, const std::wstring& child) {
    if (parent.empty()) {
        return child;
    }
    if (child.empty()) {
        return parent;
    }
    std::wstring result = TrimTrailingSlash(parent);
    result.push_back(L'\\');
    result += child;
    return result;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    // 逐级创建父目录，避免多级路径创建失败
    for (size_t i = 1; i < path.size(); ++i) {
        const wchar_t character = path[i];
        if (character != L'\\' && character != L'/') {
            continue;
        }
        if (i == 2 && path[1] == L':') {
            continue; // 盘符根目录
        }
        ::CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
    }

    if (::CreateDirectoryW(path.c_str(), nullptr)) {
        return true;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    return false;
}

std::wstring PhotoFileName(const SYSTEMTIME& time) {
    wchar_t buffer[64] = {};
    ::swprintf_s(buffer, L"IMG_%04u%02u%02u_%02u%02u%02u_%03u.jpg",
                 time.wYear, time.wMonth, time.wDay,
                 time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return std::wstring(buffer);
}

} // namespace paths
} // namespace vb
