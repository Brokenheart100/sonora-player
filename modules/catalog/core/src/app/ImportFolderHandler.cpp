#include "mamba/catalog/core/app/ImportFolderHandler.h"

#include <chrono>
#include <string>
#include <utility>

#include "mamba/catalog/core/domain/Track.h"

namespace mamba::catalog::core {
namespace {

constexpr std::string_view kNoArtist = "未知艺术家";
constexpr std::string_view kNoAlbum = "未知专辑";

// 从路径里取主文件名（去目录、去扩展名）。标题缺失时的兜底。
// 一首叫 "01.flac" 的歌比列表里的一行空白强，也比"导入失败"强。
std::string stemOf(std::string_view path)
{
    const auto slash = path.find_last_of("/\\");
    std::string_view name = (slash == std::string_view::npos) ? path : path.substr(slash + 1);
    const auto dot = name.find_last_of('.');
    if (dot != std::string_view::npos && dot > 0) {
        name = name.substr(0, dot);
    }
    return std::string(name);
}

}  // namespace

bool ImportFolderHandler::importOne(std::string_view filePath, std::int64_t addedAtUnix,
                                    ImportSummary &summary)
{
    // 先查重再读标签：重复文件是常态（重扫同一个目录），读标签是这条路径上最贵的一步。
    const auto exists = tracks_.existsByPath(filePath);
    if (!exists) {
        if (logger_ != nullptr) {
            logger_->warn("查重失败，跳过 {}：{}", filePath, exists.error().message());
        }
        ++summary.failed;
        return false;
    }
    if (exists.value()) {
        ++summary.duplicates;
        return false;
    }

    const auto metadata = reader_.read(filePath);
    if (!metadata) {
        if (logger_ != nullptr) {
            logger_->warn("读元数据失败，跳过 {}：{}", filePath, metadata.error().message());
        }
        ++summary.failed;
        return false;
    }
    const auto &meta = metadata.value();

    const auto artistId = credits_.resolveArtist(meta.artist.empty() ? kNoArtist : meta.artist);
    if (!artistId) {
        if (logger_ != nullptr) {
            logger_->warn("艺术家落库失败 {}：{}", meta.artist, artistId.error().message());
        }
        ++summary.failed;
        return false;
    }
    const auto albumId = credits_.resolveAlbum(meta.album.empty() ? kNoAlbum : meta.album);
    if (!albumId) {
        if (logger_ != nullptr) {
            logger_->warn("专辑落库失败 {}：{}", meta.album, albumId.error().message());
        }
        ++summary.failed;
        return false;
    }

    TrackDetails details;
    details.title = meta.title.empty() ? stemOf(filePath) : meta.title;
    details.artistId = artistId.value();
    details.albumId = albumId.value();
    details.durationMs = meta.durationMs;
    details.filePath = std::string(filePath);
    details.fileSizeBytes = meta.fileSizeBytes;
    details.addedAtUnix = addedAtUnix;

    auto track = Track::add(std::move(details));
    if (!track) {
        if (logger_ != nullptr) {
            logger_->warn("曲目校验失败，跳过 {}：{}", filePath, track.error().message());
        }
        ++summary.failed;
        return false;
    }

    auto stored = tracks_.add(track.value());
    if (!stored) {
        // UNIQUE(file_path) 被触发：并发导入或大小写不敏感的文件系统会走到这里。
        // 这不是错误，是"已经有了"——所以计入 duplicates 而不是 failed。
        if (stored.error().code() == kernel::ErrorCode::Conflict) {
            ++summary.duplicates;
        } else {
            if (logger_ != nullptr) {
                logger_->warn("落库失败，跳过 {}：{}", filePath, stored.error().message());
            }
            ++summary.failed;
        }
        return false;
    }

    ++summary.imported;
    return true;
}

kernel::Result<ImportSummary> ImportFolderHandler::import(const contracts::ImportFolder &command)
{
    if (command.folderPath.empty()) {
        return kernel::Result<ImportSummary>::fail(
            kernel::Error{kernel::ErrorCode::InvalidArgument, "导入路径为空"});
    }

    auto scanned = scanner_.scan(command.folderPath);
    if (!scanned) {
        return kernel::Result<ImportSummary>::fail(scanned.error());
    }
    const auto &files = scanned.value();

    kernel::UnitOfWorkScope unit(unit_);
    if (auto begun = unit.begin(); !begun) {
        return kernel::Result<ImportSummary>::fail(begun.error());
    }

    const auto addedAtUnix = std::chrono::duration_cast<std::chrono::seconds>(
                                 clock_.now().time_since_epoch())
                                 .count();

    ImportSummary summary;
    summary.filesFound = static_cast<std::int32_t>(files.size());
    for (const auto &path : files) {
        (void)importOne(path, addedAtUnix, summary);
    }

    // 全部处理完才提交：中途任何异常路径都由 UnitOfWorkScope 回滚。
    if (auto committed = unit.commit(); !committed) {
        return kernel::Result<ImportSummary>::fail(committed.error());
    }
    return kernel::Result<ImportSummary>::ok(summary);
}

kernel::Result<void> ImportFolderHandler::handle(const contracts::ImportFolder &command)
{
    auto result = import(command);
    if (!result) {
        return kernel::Result<void>::fail(result.error());
    }
    if (logger_ != nullptr) {
        const auto &summary = result.value();
        logger_->info("导入完成：扫描 {}，导入 {}，跳过 {}，失败 {}", summary.filesFound,
                      summary.imported, summary.duplicates, summary.failed);
    }
    return kernel::Result<void>::ok();
}

}  // namespace mamba::catalog::core
