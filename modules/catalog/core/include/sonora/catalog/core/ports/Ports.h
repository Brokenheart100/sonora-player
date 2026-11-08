// Catalog 的端口（应用层看得见的技术能力，但不知道谁实现）。
//
// 六个端口，各自对应一类"外部世界"：
//   ILibraryScanner  → 文件系统（枚举某目录下的音频文件）
//   IMetadataReader  → 标签解析（TagLib 或别的）
//   ICreditDirectory → 艺术家/专辑的查或建
//   ICoverStore      → 封面字节的落盘与去重（按内容寻址，同一张图只存一份）
//   ITrackRepository → 聚合的持久化（写侧，只有"按 id 取"没有"全取出"）
//   ITrackListReader → 投影查询（读侧，直接给 DTO）
//
// 最后一对分开是这套设计里最容易被质疑的一点：为什么不给仓储加 findAll()？
// 因为一旦有 findAll()，列表页就会用它，而加载一万个聚合需要几百毫秒和几十 MB
// ——它今天能用，等到曲库真的有一万首时才变成问题，那时改起来要动 UI。
// 接口层面就断掉这条路，成本是零。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

#include "sonora/catalog/contracts/Queries.h"
#include "sonora/catalog/contracts/TrackDto.h"
#include "sonora/catalog/core/domain/CatalogIds.h"
#include "sonora/catalog/core/domain/Track.h"
#include "sonora/kernel/Result.h"

namespace sonora::catalog::core {

// 从文件里读出来的原始元数据：**没有 id**，也不知道自己属于哪张专辑。
// 它和 TrackDetails 的区别就是这一层：解析器只管"文件里写了什么"，
// 把名字翻译成 id 是应用层的事。
struct TrackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::int64_t durationMs = 0;
    std::int64_t fileSizeBytes = 0;

    // 内嵌封面的原始字节（JPEG/PNG）。读不出来就是空——封面缺失不是错误，
    // 界面显示占位图。之所以让它跟着元数据一起回来：解析标签时封面已经在内存
    // 里了，为它再开一次文件（M5 首扫一万首就是一万次多余 IO）。
    std::vector<std::uint8_t> coverData;
};

// 扫描结果的一个文件。带 size 与 mtime 是刻意的：增量重扫的第一判定就是
// "这两个值变了没有"，而目录遍历时 directory_entry 本来就有它们，
// 不带回来就得对每个文件再 stat 一次（一万首就是一万次系统调用）。
struct ScannedFile {
    std::string path;   // UTF-8 绝对路径
    std::int64_t sizeBytes = 0;
    std::int64_t mtimeUnix = 0;
};

class ILibraryScanner {
public:
    virtual ~ILibraryScanner() = default;

    // 递归枚举 path 下的音频文件（扩展名白名单由实现决定）。
    // 顺序不重要，但实现应尽量稳定（便于测试与增量扫描）。
    [[nodiscard]] virtual kernel::Result<std::vector<ScannedFile>> scan(
        std::string_view folderPath) const = 0;
};

class IMetadataReader {
public:
    virtual ~IMetadataReader() = default;

    // 读不出标签时**不要失败**：返回以文件名为标题的元数据，
    // 一首没有标签的歌也比从列表里消失强。
    [[nodiscard]] virtual kernel::Result<TrackMetadata> read(std::string_view filePath) const = 0;
};

// 名字 → id 的解析：存在就取，不存在就建。这是导入路径上唯一需要"写"别的聚合
// 的地方，所以它单独成端口——将来要做"未知艺术家合并"只需要换实现。
class ICreditDirectory {
public:
    virtual ~ICreditDirectory() = default;

    [[nodiscard]] virtual kernel::Result<ArtistId> resolveArtist(std::string_view name) = 0;
    [[nodiscard]] virtual kernel::Result<AlbumId> resolveAlbum(std::string_view title) = 0;
};

// 库里已有一行的"文件指纹"。增量重扫只拿它跟扫描结果比，
// 不把整个聚合读出来（读聚合一万次才是首扫慢的真正原因）。
struct TrackFileState {
    TrackId id;
    std::int64_t fileMtimeUnix = 0;
    std::int64_t fileSizeBytes = 0;
    // 重扫时保留原始入库时间：更新一行的元数据不该让它"变成刚加进来的"。
    std::int64_t addedAtUnix = 0;
};

class ITrackRepository {
public:
    virtual ~ITrackRepository() = default;

    // 落库并回填 id。**只此一个写入口**：没有 update，因为改名走的是
    // 应用层的 load-modify-save 流程，仓储不需要猜意图。
    [[nodiscard]] virtual kernel::Result<TrackId> add(const Track &track) = 0;

    // 按 id 覆盖一行的可写字段。增量重扫发现文件变了时用它（标签变了、
    // 时长变了，id 不变——播放列表里那一行不该变成另一首歌）。
    [[nodiscard]] virtual kernel::Result<void> save(const Track &track) = 0;

    [[nodiscard]] virtual kernel::Result<Track> findById(TrackId id) const = 0;

    // 导入去重用。用路径而不是（标题+时长）：同一个文件换个标签名还是同一个文件。
    [[nodiscard]] virtual kernel::Result<bool> existsByPath(std::string_view filePath) const = 0;

    // 增量重扫的判定依据。文件不在库里返回空 optional。
    [[nodiscard]] virtual kernel::Result<std::optional<TrackFileState>> findFileState(
        std::string_view filePath) const = 0;

    [[nodiscard]] virtual kernel::Result<std::int64_t> count() const = 0;
};

// 封面的落盘与去重。按**内容**寻址：同一张专辑封面在一万首歌里出现一万次，
// 存一万份就是几百 MB 的浪费，而内容哈希天然把它们收成一份。
//
// 端口放在这里而不是 adapters，理由和别的端口一样：应用层要能说"把这堆字节收好，
// 给我一个键"，但它不该知道键是 sha 还是 fnv、文件落在哪个目录、是不是干脆塞进库。
class ICoverStore {
public:
    virtual ~ICoverStore() = default;

    // 收下封面字节，返回可存进 tracks.cover_key 的键。
    // 同一个 key 的二次调用必须是幂等的（内容寻址的必然结果）。
    // 空字节返回空串：**没有封面不是错误**。
    [[nodiscard]] virtual kernel::Result<std::string> store(
        std::span<const std::uint8_t> bytes) = 0;
};

// 读侧投影。名字里带 Reader 而不是 Repository，是因为它读的是**视图**，
// 可能来自 join、可能来自物化表，绝不来自聚合重建。
class ITrackListReader {
public:
    virtual ~ITrackListReader() = default;

    [[nodiscard]] virtual kernel::Result<contracts::TrackListResult> list(
        const contracts::ListTracks &query) const = 0;
};

}  // namespace sonora::catalog::core
