#pragma once

#include <windows.h>

#include <string>

namespace vb {
namespace core {

// 单实例控制：已存在实例时激活其窗口并返回 false
class SingleInstance {
public:
    ~SingleInstance();

    bool Acquire(const std::wstring& mutexName, const wchar_t* windowClass);

private:
    HANDLE mutex_ = nullptr;
};

} // namespace core
} // namespace vb
