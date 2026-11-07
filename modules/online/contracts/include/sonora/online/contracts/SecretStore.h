// 凭据存储端口。
//
// 明文永远不落盘：实现（platform/win/DpapiSecretStore）用 DPAPI 加密后写进
// `<AppData>/Sonora/secrets/<provider>.bin`，SQLite 里只存一个 `secret_ref`
// （见 docs/ONLINE.md §6）。
//
// 之所以是个端口而不是具体类：DPAPI 要 <windows.h>/<dpapi.h>，只许出现在
// platform/win；而 core/adapters 要能"无 Windows 也能单测"。
#pragma once

#include <string>

#include "sonora/kernel/Result.h"

namespace sonora::online::contracts
{

    class ISecretStore
    {
    public:
        virtual ~ISecretStore() = default;

        // 覆盖写。`data` 是待加密的明文字节（cookie 串），不要求是 UTF-8。
        virtual kernel::Result<void> save(const std::string &provider, const std::string &data) = 0;

        // 没有存过时返回 NotFound（而不是空串）：空串与"从没登录"要能区分开。
        virtual kernel::Result<std::string> load(const std::string &provider) = 0;

        // 不存在也算成功：登出要幂等。
        virtual kernel::Result<void> erase(const std::string &provider) = 0;
    };

} // namespace sonora::online::contracts
