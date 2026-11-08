// 把已经在运行的主窗口提到前台。
//
// 单实例判定在 host 里（QLockFile）；被判"已有实例"之后，用户该看到的是那扇窗
// 跳出来，而不是双击一次、屏幕毫无反应。跨进程找窗口只能靠 Win32，而 Win32
// 按规矩只允许出现在 platform/win（AGENTS.md 规矩 1）。
//
// 按标题找而不是按类名：本项目窗口没有自定义窗口类，标题是它唯一稳定的身份。
// 代价是标题被翻译成别的语言时找不到——那就退化成"静默退出"，不报错。
#pragma once

#include <string_view>

namespace sonora::platform::win
{

    // 找到标题匹配的顶级窗口：最小化的先还原，再置前。
    // 返回 false 表示没找到（窗口还没建出来，或标题对不上），调用方自己决定怎么说。
    bool raiseWindowByTitle(std::wstring_view title) noexcept;

} // namespace sonora::platform::win
