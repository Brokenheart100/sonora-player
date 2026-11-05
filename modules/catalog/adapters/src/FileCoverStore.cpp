#include "mamba/catalog/adapters/FileCoverStore.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace mamba::catalog::adapters
{
    namespace
    {

        namespace fs = std::filesystem;

        fs::path toPath(std::string_view utf8)
        {
            return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
        }

        std::string toUtf8(const fs::path &path)
        {
            const auto text = path.u8string();
            return std::string(reinterpret_cast<const char *>(text.data()), text.size());
        }

        // FNV-1a 64。用不着密码学强度——键只用来去重，撞一次的症状是某首歌
        // 显示成别人的封面。真要防撞就换 sha，但那意味着引一个哈希库进
        // adapters，为一个 8 字节的键不值得。
        std::uint64_t fnv1a(std::span<const std::uint8_t> bytes) noexcept
        {
            std::uint64_t hash = 0xcbf29ce484222325ULL;
            for (const std::uint8_t byte : bytes)
            {
                hash ^= byte;
                hash *= 0x100000001b3ULL;
            }
            return hash;
        }

        // 从字节头认格式，决定扩展名。认不出就 .bin——QML 的 Image 打不开它，
        // 界面落到占位图，但导入流程不受影响。
        std::string_view extensionOf(std::span<const std::uint8_t> bytes) noexcept
        {
            if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
            {
                return "jpg";
            }
            if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' &&
                bytes[3] == 'G')
            {
                return "png";
            }
            if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
            {
                return "bmp";
            }
            if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' &&
                bytes[3] == 'F' && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' &&
                bytes[11] == 'P')
            {
                return "webp";
            }
            return "bin";
        }

    } // namespace

    FileCoverStore::FileCoverStore(std::string rootDirectory) : root_(std::move(rootDirectory))
    {
    }

    std::string FileCoverStore::pathFor(std::string_view key) const
    {
        if (key.empty() || root_.empty())
        {
            return {};
        }
        return root_ + "/" + std::string(key);
    }

    kernel::Result<std::string> FileCoverStore::store(std::span<const std::uint8_t> bytes)
    {
        // 没有封面不是错误。空键让上面的路径逻辑一路短路到"显示占位图"。
        if (bytes.empty() || root_.empty())
        {
            return kernel::Result<std::string>::ok(std::string{});
        }

        const std::uint64_t hash = fnv1a(bytes);
        std::string key = std::format("{:016x}.{}", hash, extensionOf(bytes));

        std::error_code ec;
        const fs::path directory = toPath(root_);
        if (!fs::is_directory(directory, ec))
        {
            fs::create_directories(directory, ec);
            if (ec)
            {
                return kernel::Error{kernel::ErrorCode::Unavailable,
                                     std::format("无法创建封面目录 {}：{}", root_, ec.message())};
            }
        }

        const fs::path target = toPath(pathFor(key));
        // 内容寻址的必然结果：已经存在就一定是同一份内容，直接复用。
        if (fs::exists(target, ec) && !ec)
        {
            return kernel::Result<std::string>::ok(std::move(key));
        }

        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return kernel::Error{kernel::ErrorCode::Unavailable,
                                 std::format("无法写入封面 {}", toUtf8(target))};
        }
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out)
        {
            return kernel::Error{kernel::ErrorCode::Unavailable,
                                 std::format("封面写入不完整 {}", toUtf8(target))};
        }

        return kernel::Result<std::string>::ok(std::move(key));
    }

} // namespace mamba::catalog::adapters
