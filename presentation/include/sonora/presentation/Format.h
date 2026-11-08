// 表现层的显示格式：目前只有"毫秒 → m:ss"。
//
// 曾经在 TrackListModel / PlayerViewModel / OnlineViewModel 各写了一份（三份逐字相同），
// 结果是改一次格式要改三处、而且"未知时长显示什么"很容易只改到一半。
#pragma once

#include <QString>

#include <cstdint>

namespace sonora::presentation {

// 毫秒 → "3:45"；<= 0 表示时长未知，返回 "--:--"。
// 超过一小时也只是分钟数变大（"72:05"），不另开小时段——电台/播客里更常见。
[[nodiscard]] inline QString formatDuration(std::int64_t milliseconds)
{
    if (milliseconds <= 0) {
        return QStringLiteral("--:--");
    }
    const auto totalSeconds = milliseconds / 1000;
    const auto seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace sonora::presentation
