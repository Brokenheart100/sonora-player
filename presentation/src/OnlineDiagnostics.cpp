#include "sonora/presentation/OnlineViewModel.h"
#include "OnlineViewModelShared.h"

#include <QCoreApplication>
#include <QVariantList>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sonora/kernel/Error.h"
#include "sonora/online/contracts/Commands.h"
#include "sonora/online/contracts/Queries.h"
#include "sonora/playback/contracts/Commands.h"
#include "sonora/playback/contracts/Queries.h"
#include "sonora/presentation/Format.h"

namespace sonora::presentation
{
    using namespace detail;

    // —— 诊断快照 ——

    void OnlineViewModel::refreshDiagnostics()
    {
        const auto snapshot =
            queries_.ask<online::contracts::OnlineDiagnosticsDto>(online::contracts::GetOnlineDiagnostics{});
        if (!snapshot)
        {
            return;
        }
        const auto &value = snapshot.value();

        QVariantMap out;
        out.insert(QStringLiteral("provider"), fromUtf8(value.provider));
        out.insert(QStringLiteral("consecutiveFailures"), value.consecutiveFailures);
        out.insert(QStringLiteral("nextRetryAtUnix"), static_cast<qlonglong>(value.nextRetryAtUnix));
        out.insert(QStringLiteral("tokensLeft"), value.tokensLeft);
        out.insert(QStringLiteral("tokenBurst"), value.tokenBurst);
        out.insert(QStringLiteral("cacheRows"), value.cache.searchRows);
        out.insert(QStringLiteral("cacheBytes"), static_cast<qlonglong>(value.cache.searchBytes));
        out.insert(QStringLiteral("capRows"), value.cache.capRows);
        out.insert(QStringLiteral("capBytes"), static_cast<qlonglong>(value.cache.capBytes));
        out.insert(QStringLiteral("songRows"), value.cache.songRows);
        out.insert(QStringLiteral("detailRows"), value.cache.detailRows);

        const auto &d = value.degradations;
        QVariantMap degradation;
        degradation.insert(QStringLiteral("offline"), d.offline);
        degradation.insert(QStringLiteral("timeout"), d.timeout);
        degradation.insert(QStringLiteral("rateLimited"), d.rateLimited);
        degradation.insert(QStringLiteral("unauthenticated"), d.unauthenticated);
        degradation.insert(QStringLiteral("upstream"), d.upstream);
        degradation.insert(QStringLiteral("urlExpired"), d.urlExpired);
        degradation.insert(QStringLiteral("cacheHits"), d.cacheHits);
        degradation.insert(QStringLiteral("totalErrors"), d.totalErrors);
        out.insert(QStringLiteral("degradations"), degradation);

        // 时间只给 Unix 秒，界面自己写"3 分钟前"——ViewModel 不持时钟。
        QVariantList recent;
        recent.reserve(static_cast<qsizetype>(value.recentErrors.size()));
        for (const auto &record : value.recentErrors)
        {
            QVariantMap item;
            item.insert(QStringLiteral("at"), static_cast<qlonglong>(record.atUnix));
            item.insert(QStringLiteral("where"), fromUtf8(record.where));
            item.insert(QStringLiteral("message"), fromUtf8(record.error.message));
            item.insert(QStringLiteral("retryable"), record.error.retryable);
            recent.append(item);
        }
        out.insert(QStringLiteral("recentErrors"), recent);
        // 写操作单独两个数：收藏成功/失败不混进读路径的降级统计。
        out.insert(QStringLiteral("favoriteWrites"), value.favoriteWrites);
        out.insert(QStringLiteral("favoriteFailures"), value.favoriteFailures);

        diagnostics_ = std::move(out);
        emit diagnosticsChanged();
    }

} // namespace sonora::presentation
