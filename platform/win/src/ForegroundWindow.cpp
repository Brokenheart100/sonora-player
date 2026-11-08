#include "sonora/platform/win/ForegroundWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace sonora::platform::win
{

    bool raiseWindowByTitle(std::wstring_view title) noexcept
    {
        // FindWindowW 要的是 null 结尾，string_view 不保证，复制一份。
        const std::wstring owned{title};
        const HWND window = ::FindWindowW(nullptr, owned.c_str());
        if (window == nullptr)
        {
            return false;
        }

        // 最小化的窗口 SetForegroundWindow 也会"成功"，但用户看到的还是任务栏里
        // 一条——先还原。SW_SHOW 不够：被最小化过的窗口需要 SW_RESTORE 才有尺寸。
        if (::IsIconic(window) != 0)
        {
            ::ShowWindow(window, SW_RESTORE);
        }
        ::SetForegroundWindow(window);
        return true;
    }

} // namespace sonora::platform::win
