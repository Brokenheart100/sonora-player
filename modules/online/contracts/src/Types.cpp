// 枚举名字的唯一实现。
//
// 放在 contracts 里而不是各家用 switch：日志、诊断、界面层的调试输出都读同一张表，
// 少一处就是一处日志对不上。
#include "sonora/online/contracts/Types.h"

namespace sonora::online::contracts
{

    const char *toName(JobPhase phase) noexcept
    {
        switch (phase)
        {
        case JobPhase::Idle:
            return "Idle";
        case JobPhase::Loading:
            return "Loading";
        case JobPhase::LoadingMore:
            return "LoadingMore";
        case JobPhase::Ready:
            return "Ready";
        case JobPhase::Empty:
            return "Empty";
        case JobPhase::Error:
            return "Error";
        case JobPhase::Offline:
            return "Offline";
        }
        return "Unknown";
    }

    const char *toName(AccountPhase phase) noexcept
    {
        switch (phase)
        {
        case AccountPhase::Guest:
            return "Guest";
        case AccountPhase::QrWaiting:
            return "QrWaiting";
        case AccountPhase::QrScanned:
            return "QrScanned";
        case AccountPhase::QrExpired:
            return "QrExpired";
        case AccountPhase::LoggedIn:
            return "LoggedIn";
        case AccountPhase::Expiring:
            return "Expiring";
        case AccountPhase::Failed:
            return "Failed";
        }
        return "Unknown";
    }

    const char *toName(SourceKind kind) noexcept
    {
        switch (kind)
        {
        case SourceKind::Search:
            return "Search";
        case SourceKind::Playlist:
            return "Playlist";
        case SourceKind::Favorites:
            return "Favorites";
        case SourceKind::Recent:
            return "Recent";
        case SourceKind::DailyRecommend:
            return "DailyRecommend";
        case SourceKind::Album:
            return "Album";
        case SourceKind::Chart:
            return "Chart";
        case SourceKind::Artist:
            return "Artist";
        case SourceKind::NewSongs:
            return "NewSongs";
        }
        return "Unknown";
    }

} // namespace sonora::online::contracts
