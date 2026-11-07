#include "sonora/platform/win/DpapiSecretStore.h"

#include <windows.h>
#include <dpapi.h>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>

namespace sonora::platform::win
{
    namespace
    {
        // 附加熵：同一账户下别的程序也调 DPAPI，加熵能把密文绑到"本程序的用途"上。
        constexpr unsigned char kEntropy[] = {'s', 'o', 'n', 'o', 'r', 'a', '.', 'o', 'n',
                                              'l', 'i', 'n', 'e', '.', 'v', '1'};

        kernel::Result<void> failure(const char *what)
        {
            return kernel::Error{kernel::ErrorCode::Internal, what};
        }

        DATA_BLOB asBlob(const void *data, std::size_t size)
        {
            DATA_BLOB blob{};
            blob.pbData = const_cast<BYTE *>(static_cast<const BYTE *>(data));
            blob.cbData = static_cast<DWORD>(size);
            return blob;
        }

        // provider 直接当文件名用，所以只放行 [a-z0-9-]；别的一律拒绝，
        // 免得将来有人传 "../.." 之类的东西进来。
        bool isSafeProvider(const std::string &provider)
        {
            if (provider.empty() || provider.size() > 32)
            {
                return false;
            }
            for (char c : provider)
            {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
                if (!ok)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    DpapiSecretStore::DpapiSecretStore(std::filesystem::path directory)
        : directory_(std::move(directory))
    {
    }

    std::filesystem::path DpapiSecretStore::fileFor(const std::string &provider) const
    {
        return directory_ / (provider + ".bin");
    }

    kernel::Result<void> DpapiSecretStore::save(const std::string &provider, const std::string &data)
    {
        if (!isSafeProvider(provider))
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "provider 名字不合法"};
        }

        DATA_BLOB plain = asBlob(data.data(), data.size());
        DATA_BLOB entropy = asBlob(kEntropy, sizeof(kEntropy));
        DATA_BLOB cipher{};

        if (::CryptProtectData(&plain, L"Sonora 在线账号凭据", &entropy, nullptr, nullptr,
                               CRYPTPROTECT_UI_FORBIDDEN, &cipher) == FALSE)
        {
            return failure("CryptProtectData 失败");
        }

        std::error_code ec;
        std::filesystem::create_directories(directory_, ec);
        if (ec)
        {
            ::LocalFree(cipher.pbData);
            return failure("创建 secrets 目录失败");
        }

        // 先写临时文件再原子替换：中途崩溃不会留下半个密文。
        const std::filesystem::path target = fileFor(provider);
        const std::filesystem::path temp = target.string() + ".tmp";
        {
            std::ofstream out{temp, std::ios::binary | std::ios::trunc};
            if (!out)
            {
                ::LocalFree(cipher.pbData);
                return failure("打开密文文件失败");
            }
            out.write(reinterpret_cast<const char *>(cipher.pbData),
                      static_cast<std::streamsize>(cipher.cbData));
            if (!out)
            {
                ::LocalFree(cipher.pbData);
                std::filesystem::remove(temp, ec);
                return failure("写密文文件失败");
            }
        }
        ::LocalFree(cipher.pbData);

        if (::MoveFileExW(temp.wstring().c_str(), target.wstring().c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
        {
            std::filesystem::remove(temp, ec);
            return failure("替换密文文件失败");
        }
        return {};
    }

    kernel::Result<std::string> DpapiSecretStore::load(const std::string &provider)
    {
        if (!isSafeProvider(provider))
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "provider 名字不合法"};
        }

        const std::filesystem::path target = fileFor(provider);
        std::ifstream in{target, std::ios::binary};
        if (!in)
        {
            return kernel::Error{kernel::ErrorCode::NotFound, "没有存过凭据"};
        }

        std::vector<char> buffer{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        if (buffer.empty())
        {
            return kernel::Error{kernel::ErrorCode::NotFound, "凭据文件是空的"};
        }

        DATA_BLOB cipher = asBlob(buffer.data(), buffer.size());
        DATA_BLOB entropy = asBlob(kEntropy, sizeof(kEntropy));
        DATA_BLOB plain{};

        if (::CryptUnprotectData(&cipher, nullptr, &entropy, nullptr, nullptr,
                                 CRYPTPROTECT_UI_FORBIDDEN, &plain) == FALSE)
        {
            return kernel::Error{kernel::ErrorCode::Unauthenticated,
                                 "凭据解不开（换了账户或换了机器）"};
        }

        std::string result{reinterpret_cast<const char *>(plain.pbData), plain.cbData};
        ::SecureZeroMemory(plain.pbData, plain.cbData);
        ::LocalFree(plain.pbData);
        ::SecureZeroMemory(buffer.data(), buffer.size());
        return kernel::Result<std::string>::ok(std::move(result));
    }

    kernel::Result<void> DpapiSecretStore::erase(const std::string &provider)
    {
        if (!isSafeProvider(provider))
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "provider 名字不合法"};
        }

        std::error_code ec;
        const std::filesystem::path target = fileFor(provider);
        std::filesystem::remove(target, ec);
        // 不存在也算成功：登出要幂等。
        return {};
    }

} // namespace sonora::platform::win
