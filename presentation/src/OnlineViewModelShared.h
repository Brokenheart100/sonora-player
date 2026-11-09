#pragma once

// OnlineViewModel 拆分后的共用小工具。只为 presentation/src 下几个 .cpp 服务，
// 不放进 include/ —— 它们不是任何模块的契约。

#include <QCoreApplication>
#include <QString>

#include <string>
#include <vector>

#include "sonora/online/contracts/Types.h"

namespace sonora::presentation::detail
{
    using online::contracts::AccountPhase;
    using online::contracts::JobPhase;
    using online::contracts::OnlineError;
    using online::contracts::SourceKind;

    inline QString fromUtf8(const std::string &text) { return QString::fromStdString(text); }

    // 与服务端的 "loaded / total" 对齐：total 为 0 时说明接口没给总数，
    // 那就只报"已加载 N 条"，不要编一个 0/0。
    inline QString countText(const online::contracts::OnlineJobState &state)
    {
        if (state.total > 0)
        {
            return QCoreApplication::translate("OnlineViewModel", "%1 / %2 首").arg(state.loaded).arg(state.total);
        }
        return QCoreApplication::translate("OnlineViewModel", "%n 首", "", static_cast<int>(state.loaded));
    }

    // QML 里传的是字符串（锚点跟着标签页走），到这里换成枚举；
    // 认不出来就退回搜索——总比静默什么都不做要好查。
    inline online::contracts::SourceKind kindFromName(const QString &name)
    {
        using online::contracts::SourceKind;
        if (name == QLatin1String("chart")) { return SourceKind::Chart; }
        if (name == QLatin1String("album")) { return SourceKind::Album; }
        if (name == QLatin1String("artist")) { return SourceKind::Artist; }
        if (name == QLatin1String("newsongs")) { return SourceKind::NewSongs; }
        if (name == QLatin1String("playlist")) { return SourceKind::Playlist; }
        return SourceKind::Search;
    }

    inline bool isBusy(online::contracts::JobPhase phase)
    {
        return phase == online::contracts::JobPhase::Loading || phase == online::contracts::JobPhase::LoadingMore;
    }

    // 排队与播放共用"解析整队直链"这一步：解析回来时，`applyPlayback`
    // 按 enqueueOnly_ 决定派发 AppendTracks 还是 PlayTracks。
    inline std::vector<std::string> idsOfRows(const std::vector<online::contracts::OnlineTrackDto> &rows)
    {
        auto ids = std::vector<std::string>{};
        ids.reserve(rows.size());
        for (const auto &row : rows) { ids.push_back(row.songId); }
        return ids;
    }
} // namespace sonora::presentation::detail
