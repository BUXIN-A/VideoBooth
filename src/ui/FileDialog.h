#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include <shobjidl.h>
#include <wrl/client.h>

#include "ui/TopmostScope.h"

namespace vb {
namespace ui {

// 系统文件对话框的薄封装。全屏展台窗口下普通对话框可能被压在画面之下，
// 因此显示期间统一用 TopmostScope 把主窗口临时提到最顶层。

// 选择文件夹；initialPath 非空时作为打开位置。用户取消时返回 false
inline bool PickFolderDialog(HWND owner, const std::wstring& initialPath, std::wstring& path) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return false;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (!initialPath.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(::SHCreateItemFromParsingName(initialPath.c_str(), nullptr,
                                                   IID_PPV_ARGS(folder.GetAddressOf())))) {
            dialog->SetFolder(folder.Get());
        }
    }
    const TopmostScope topmost(owner);
    if (dialog->Show(owner) != S_OK) {
        return false;
    }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.GetAddressOf()))) {
        return false;
    }
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || raw == nullptr) {
        return false;
    }
    path.assign(raw);
    ::CoTaskMemFree(raw);
    return !path.empty();
}

// 选择图片文件（支持 jpg/jpeg/png/bmp）；initialPath 非空时作为打开位置。
// allowMultiple 为 true 时支持多选。用户取消时返回 false
inline bool PickImageDialog(HWND owner, const std::wstring& initialPath, bool allowMultiple,
                            std::vector<std::wstring>& paths) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return false;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
    if (allowMultiple) {
        options |= FOS_ALLOWMULTISELECT;
    }
    dialog->SetOptions(options);
    const COMDLG_FILTERSPEC filters[] = {{L"图片文件", L"*.jpg;*.jpeg;*.png;*.bmp"},
                                         {L"所有文件", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    if (!initialPath.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(::SHCreateItemFromParsingName(initialPath.c_str(), nullptr,
                                                   IID_PPV_ARGS(folder.GetAddressOf())))) {
            dialog->SetFolder(folder.Get());
        }
    }
    const TopmostScope topmost(owner);
    if (dialog->Show(owner) != S_OK) {
        return false;
    }
    ComPtr<IShellItemArray> items;
    if (FAILED(dialog->GetResults(items.GetAddressOf())) || items == nullptr) {
        return false;
    }
    DWORD count = 0;
    if (FAILED(items->GetCount(&count))) {
        return false;
    }
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        if (FAILED(items->GetItemAt(i, item.GetAddressOf()))) {
            continue;
        }
        PWSTR raw = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || raw == nullptr) {
            continue;
        }
        paths.emplace_back(raw);
        ::CoTaskMemFree(raw);
    }
    return !paths.empty();
}

} // namespace ui
} // namespace vb
