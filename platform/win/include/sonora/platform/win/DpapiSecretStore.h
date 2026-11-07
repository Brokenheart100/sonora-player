// DPAPI 实现的凭据存储。
//
// 明文（cookie 串）只在内存里出现，落盘的是 CryptProtectData 之后的密文：
// `<dir>/<provider>.bin`。密钥由 Windows 按当前用户派生，别的账户或换台机器都
// 解不开 —— 这是"不把密码学搬进项目"的最低成本做法（见 docs/ONLINE.md §6）。
//
// 宿主要保证 `<dir>` 是应用数据目录（`%APPDATA%\Sonora\Sonora\secrets`），
// 不要放进程序目录：程序目录可能是只读的，而且升级时会被覆盖。
#pragma once

#include <filesystem>
#include <string>

#include "sonora/kernel/Result.h"
#include "sonora/online/contracts/SecretStore.h"

namespace sonora::platform::win
{

    class DpapiSecretStore final : public online::contracts::ISecretStore
    {
    public:
        explicit DpapiSecretStore(std::filesystem::path directory);

        kernel::Result<void> save(const std::string &provider, const std::string &data) override;

        kernel::Result<std::string> load(const std::string &provider) override;

        kernel::Result<void> erase(const std::string &provider) override;

    private:
        [[nodiscard]] std::filesystem::path fileFor(const std::string &provider) const;

        std::filesystem::path directory_;
    };

} // namespace sonora::platform::win
