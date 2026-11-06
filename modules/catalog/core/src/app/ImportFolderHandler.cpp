#include "sonora/catalog/core/app/ImportFolderHandler.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "sonora/catalog/core/domain/Track.h"

namespace sonora::catalog::core {
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

kernel::Result<TrackDetails> ImportFolderHandler::buildDetails(const ScannedFile &file,
                                                               std::int64_t addedAtUnix,
                                                               TrackId existingId)
{
    const auto metadata = reader_.read(file.path);
    if (!metadata) {
        return kernel::Result<TrackDetails>::fail(metadata.error());
    }
    const auto &meta = metadata.value();

    const auto artistId = credits_.resolveArtist(meta.artist.empty() ? kNoArtist : meta.artist);
    if (!artistId) {
        return kernel::Result<TrackDetails>::fail(artistId.error());
    }
    const auto albumId = credits_.resolveAlbum(meta.album.empty() ? kNoAlbum : meta.album);
    if (!albumId) {
        return kernel::Result<TrackDetails>::fail(albumId.error());
    }

    TrackDetails details;
    details.title = meta.title.empty() ? stemOf(file.path) : meta.title;
    details.artistId = artistId.value();
    details.albumId = albumId.value();
    details.durationMs = meta.durationMs;
    details.filePath = file.path;
    // 大小与时间戳以**扫描结果**为准，不信标签里读出来的值：
    // 增量判定的两边必须是同一个来源，否则文件被替换后永远判"没变"。
    details.fileSizeBytes = file.sizeBytes;
    details.fileMtimeUnix = file.mtimeUnix;
    details.addedAtUnix = addedAtUnix;
    (void)existingId;
    // 封面：字节收进 ICoverStore，库里只留键。
    // **存不下不算失败**——封面是列表上的一个方块，不是这首歌能不能听的条件。
    if (covers_ != nullptr && !meta.coverData.empty()) {
        auto stored = covers_->store(meta.coverData);
        if (stored) {
            details.coverKey = std::move(stored).value();
        } else if (logger_ != nullptr) {
            logger_->warn("封面存不下（{}），{} 先不带封面入库", stored.error().message(),
                          file.path);
        }
    }
    return kernel::Result<TrackDetails>::ok(std::move(details));
}

bool ImportFolderHandler::importOne(const ScannedFile &file, std::int64_t addedAtUnix,
                                    ImportSummary &summary)
{
    // 第一步永远是查指纹，不是读标签：读标签是这条路径上最贵的一步。
    const auto state = tracks_.findFileState(file.path);
    if (!state) {
        if (logger_ != nullptr) {
            logger_->warn("查重失败，跳过 {}：{}", file.path, state.error().message());
        }
        ++summary.failed;
        return false;
    }

    if (!state.value().has_value()) {
        // 库里没有 → 新增。
        auto details = buildDetails(file, addedAtUnix, TrackId{});
        if (!details) {
            if (logger_ != nullptr) {
                logger_->warn("读元数据失败，跳过 {}：{}", file.path, details.error().message());
            }
            ++summary.failed;
            return false;
        }
        auto track = Track::add(std::move(details).value());
        if (!track) {
            if (logger_ != nullptr) {
                logger_->warn("曲目校验失败，跳过 {}：{}", file.path, track.error().message());
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
                    logger_->warn("落库失败，跳过 {}：{}", file.path, stored.error().message());
                }
                ++summary.failed;
            }
            return false;
        }
        ++summary.imported;
        return true;
    }

    // 库里已有 → 比对指纹。
    const auto &existing = *state.value();
    if (existing.fileMtimeUnix == file.mtimeUnix && existing.fileSizeBytes == file.sizeBytes) {
        // 没变就是没变：连标签都不读。二次扫描一万首快在这里，而不是快在 SQL 上。
        ++summary.unchanged;
        return false;
    }

    // 变了 → 重读标签，用**原来的 id 与原入库时间**覆盖，保住它在播放列表里的身份。
    auto details = buildDetails(file, existing.addedAtUnix, existing.id);
    if (!details) {
        if (logger_ != nullptr) {
            logger_->warn("重扫读元数据失败，跳过 {}：{}", file.path, details.error().message());
        }
        ++summary.failed;
        return false;
    }
    auto track = Track::rehydrate(existing.id, std::move(details).value());
    if (!track) {
        if (logger_ != nullptr) {
            logger_->warn("重扫校验失败，跳过 {}：{}", file.path, track.error().message());
        }
        ++summary.failed;
        return false;
    }
    if (auto saved = tracks_.save(track.value()); !saved) {
        if (logger_ != nullptr) {
            logger_->warn("重扫落库失败，跳过 {}：{}", file.path, saved.error().message());
        }
        ++summary.failed;
        return false;
    }
    ++summary.updated;
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

    ImportSummary summary;
    summary.filesFound = static_cast<std::int32_t>(files.size());

    running_.store(true);
    filesFound_.store(summary.filesFound);
    processed_.store(0);
    imported_.store(0);
    updated_.store(0);

    const auto addedAtUnix =
        std::chrono::duration_cast<std::chrono::seconds>(clock_.now().time_since_epoch()).count();

    std::int32_t processed = 0;
    for (std::size_t begin = 0; begin < files.size(); begin += static_cast<std::size_t>(batchSize_)) {
        const std::size_t end = std::min(files.size(), begin + static_cast<std::size_t>(batchSize_));

        // 一批一个事务：中途断电最多丢一批，而且一次提交不会锁库几秒钟。
        kernel::UnitOfWorkScope unit(unit_);
        if (auto begun = unit.begin(); !begun) {
            running_.store(false);
            return kernel::Result<ImportSummary>::fail(begun.error());
        }
        for (std::size_t i = begin; i < end; ++i) {
            (void)importOne(files[i], addedAtUnix, summary);
            processed_.store(++processed);
        }
        if (auto committed = unit.commit(); !committed) {
            running_.store(false);
            return kernel::Result<ImportSummary>::fail(committed.error());
        }
    }

    imported_.store(summary.imported);
    updated_.store(summary.updated);
    running_.store(false);

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
        logger_->info("导入完成：扫描 {}，新增 {}，更新 {}，未变 {}，重复 {}，失败 {}",
                      summary.filesFound, summary.imported, summary.updated, summary.unchanged,
                      summary.duplicates, summary.failed);
    }
    return kernel::Result<void>::ok();
}

ImportProgress ImportFolderHandler::progress() const noexcept
{
    ImportProgress snapshot;
    snapshot.running = running_.load();
    snapshot.filesFound = filesFound_.load();
    snapshot.processed = processed_.load();
    snapshot.imported = imported_.load();
    snapshot.updated = updated_.load();
    return snapshot;
}

}  // namespace sonora::catalog::core
