#include "sonora/online/adapters/OnlineService.h"
#include "OnlineServiceShared.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "sonora/online/core/app/Hint.h"
#include "sonora/online/core/auth/CookieHeader.h"
#include "sonora/online/core/auth/QrCode.h"
#include "sonora/online/core/provider/NeteaseUrls.h"
#include "sonora/platform/storage/SqliteStatement.h"

namespace sonora::online::adapters
{
    using namespace detail;

    namespace c = online::contracts;

    // —— 诊断快照（连续失败、令牌、缓存、最近错误） ——

    kernel::Result<c::OnlineDiagnosticsDto> OnlineService::handle(const c::GetOnlineDiagnostics &query)
    {
        const std::int64_t now = nowMs();
        c::OnlineDiagnosticsDto out;
        out.provider = query.provider.empty() ? std::string{kProvider} : query.provider;
        out.degradations = diag_.counters();
        out.consecutiveFailures = failures_.consecutive;
        out.nextRetryAtUnix = failures_.nextRetryAtMs > 0 ? (failures_.nextRetryAtMs + 999) / 1000 : 0;
        out.tokensLeft = bucket_.tokens(now);
        out.tokenBurst = 6.0;

        const auto stats = store_.stats();
        out.cache.searchRows = stats.searchRows;
        out.cache.searchBytes = stats.searchBytes;
        out.cache.songRows = stats.songRows;
        out.cache.detailRows = stats.detailRows;
        out.cache.capRows = static_cast<std::int32_t>(SqliteOnlineStore::kMaxSearchRows);
        out.cache.capBytes = SqliteOnlineStore::kMaxSearchBytes;

        out.favoriteWrites = diag_.favoriteWrites();
        out.favoriteFailures = diag_.favoriteFailures();
        out.recentErrors = diag_.recent();
        return kernel::Result<c::OnlineDiagnosticsDto>::ok(std::move(out));
    }


} // namespace sonora::online::adapters
