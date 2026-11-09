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

    // —— 登录、登出、扫码 ——

    void OnlineViewModel::signOut()
    {
        dispatchQuietly(
            [this]
            {
                online::contracts::SignOut command;
                return commands_.dispatch(command);
            });
        refreshStatus();
    }

    void OnlineViewModel::signIn(const QString &cookie)
    {
        loginError_.clear();
        signingIn_ = true;
        online::contracts::SignInWithCookie command;
        command.provider = "ncm";
        command.cookie = cookie.toStdString();
        const auto result = commands_.dispatch(command);
        if (!result)
        {
            signingIn_ = false;
            loginError_ = fromUtf8(result.error().message());
        }
        emit statusChanged();
    }

    bool OnlineViewModel::qrCell(int row, int col) const
    {
        if (row < 0 || col < 0 || row >= qrSize_ || col >= qrSize_)
        {
            return false;
        }
        const std::string &line = qrRows_[static_cast<std::size_t>(row)];
        if (static_cast<std::size_t>(col) >= line.size())
        {
            return false;
        }
        return line[static_cast<std::size_t>(col)] == '1';
    }

    void OnlineViewModel::startQrLogin()
    {
        dispatchQuietly([this] { return commands_.dispatch(online::contracts::StartQrLogin{}); });
        qrActive_ = true;
        qrRows_.clear();
        qrSize_ = 0;
        qrSession_ = 0;
        qrMessage_ = tr("正在生成二维码…");
        loginError_.clear();
        emit qrChanged();
    }

    void OnlineViewModel::cancelQrLogin()
    {
        dispatchQuietly([this] { return commands_.dispatch(online::contracts::CancelQrLogin{}); });
        qrActive_ = false;
        emit qrChanged();
    }


} // namespace sonora::presentation
