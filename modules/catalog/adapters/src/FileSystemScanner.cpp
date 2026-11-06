#include "sonora/catalog/adapters/FileSystemScanner.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace sonora::catalog::adapters
{
    namespace
    {

        namespace fs = std::filesystem;

        // std::string 里放的是 UTF-8；Windows 的文件路径是 UTF-16。
        // 这两个函数是唯一的转换点——散落在各处的话，中文路径总有一天会变问号。
        fs::path toPath(std::string_view utf8)
        {
            return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
        }

        std::string toUtf8(const fs::path &path)
        {
            const auto text = path.u8string();
            return std::string(reinterpret_cast<const char *>(text.data()), text.size());
        }

        std::string lowerExtension(const fs::path &path)
        {
            auto ext = toUtf8(path.extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return ext;
        }

    } // namespace

    FileSystemScanner::FileSystemScanner(std::vector<std::string> extensions)
        : extensions_(extensions.empty() ? defaultExtensions() : std::move(extensions))
    {
    }

    std::vector<std::string> FileSystemScanner::defaultExtensions()
    {
        // 以"能不能被 FFmpeg 打开"为准，不是以"我们有没有专门实现"为准：
        // 解码能力是 FFmpeg 给的，白名单窄了只会让用户觉得"这歌导入不进来"。
        return {".flac", ".mp3", ".m4a", ".aac", ".ogg", ".opus", ".wav", ".wma", ".ape", ".alac",
                ".aiff", ".dsf"};
    }

    kernel::Result<std::vector<core::ScannedFile>> FileSystemScanner::scan(
        std::string_view folderPath) const
    {
        if (folderPath.empty())
        {
            return kernel::Result<std::vector<core::ScannedFile>>::fail(
                kernel::Error{kernel::ErrorCode::InvalidArgument, "扫描路径为空"});
        }

        const auto root = toPath(folderPath);
        std::error_code ec;
        if (!fs::exists(root, ec) || ec)
        {
            return kernel::Result<std::vector<core::ScannedFile>>::fail(
                kernel::Error{kernel::ErrorCode::NotFound, "目录不存在：" + std::string(folderPath)});
        }
        if (!fs::is_directory(root, ec) || ec)
        {
            return kernel::Result<std::vector<core::ScannedFile>>::fail(
                kernel::Error{kernel::ErrorCode::InvalidArgument, "不是目录：" + std::string(folderPath)});
        }

        std::vector<core::ScannedFile> files;
        // 用 error_code 重载而不是异常版本：一个删到一半的目录不该让整个导入崩掉。
        fs::recursive_directory_iterator it{root, fs::directory_options::skip_permission_denied, ec};
        if (ec)
        {
            return kernel::Result<std::vector<core::ScannedFile>>::fail(
                kernel::Error{kernel::ErrorCode::Unavailable, "无法遍历目录：" + ec.message()});
        }

        const fs::recursive_directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue; // 单个子目录读不了就跳过，不放弃整次扫描
            }
            std::error_code typeEc;
            if (!it->is_regular_file(typeEc) || typeEc)
            {
                continue;
            }
            const auto ext = lowerExtension(it->path());
            if (std::find(extensions_.begin(), extensions_.end(), ext) == extensions_.end())
            {
                continue;
            }
            // 一律存绝对路径。用户传进来的可能是 "."、"music" 这类相对路径，
            // 而这条字符串要进库、下次启动还要拿它去解码——那时工作目录早就变了。
            std::error_code absEc;
            const auto absolutePath = fs::absolute(it->path(), absEc);

            core::ScannedFile file;
            file.path = toUtf8(absEc ? it->path() : absolutePath);

            // 大小与 mtime 在目录遍历时一起拿：增量重扫靠它俩判定，而重新 stat
            // 一万个文件跟重新遍历一遍目录是同一量级的开销。
            std::error_code sizeEc;
            const auto size = it->file_size(sizeEc);
            file.sizeBytes = sizeEc ? 0 : static_cast<std::int64_t>(size);

            std::error_code timeEc;
            const auto fileTime = it->last_write_time(timeEc);
            if (!timeEc)
            {
                // file_time_type 的纪元在标准里没规定，只能靠"同一时刻的两个钟"换算：
                // 取两个 now() 的差，再用系统时钟的 now() 减掉它。
                const auto fileNow = fs::file_time_type::clock::now();
                const auto systemNow = std::chrono::system_clock::now();
                const auto systemTime = systemNow - (fileNow - fileTime);
                file.mtimeUnix = std::chrono::duration_cast<std::chrono::seconds>(
                                     systemTime.time_since_epoch())
                                     .count();
            }
            files.push_back(std::move(file));
        }

        // 排序不是洁癖：导入顺序决定了 added_at 相同的行在列表里的相对位置，
        // 不稳定的话每次开发机上的输出都不一样，测试也就没法断言。
        std::sort(files.begin(), files.end(),
                  [](const core::ScannedFile &left, const core::ScannedFile &right)
                  {
                      return left.path < right.path;
                  });
        return kernel::Result<std::vector<core::ScannedFile>>::ok(std::move(files));
    }

} // namespace sonora::catalog::adapters
