// 在线音源的跨模块契约：枚举与 DTO。
//
// 三条约束，破了任意一条都说明这一层被穿透了：
//
//   1. **零 Qt**。表现层、`online/core`、`sonora_tests` 都直接 include 本头文件，
//      而测试二进制的价值就在"不加载 Qt DLL 也能跑"。
//   2. **零 JSON**。契约里只有值，解析住在 adapters/core（理由见
//      docs/ONLINE.md §9.2：JSON 解析放零 Qt 的代码里，夹具单测才跑得进 sonora_tests）。
//   3. **id 一律是 `std::string`**。网易云的 songId 会长到超过 double 的整数精度
//      （JS 客户端那边早就踩过），也没有理由让每个使用方去关心它到底是数字还是字符串。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sonora/kernel/Error.h"

namespace sonora::online::contracts
{

    // 一次在线任务（搜索 / 歌单 / 每日推荐）的生命周期。
    // 界面只需要读它，不需要知道底下是哪家服务。
    enum class JobPhase : std::uint8_t
    {
        Idle,        // 还没发过请求
        Loading,     // 首页在飞（页面上还没有行）
        LoadingMore, // 下一页在飞（已有行仍然可读，不能清空它）
        Ready,       // 有数据，且可能还有下一页
        Empty,       // 请求成功但零结果——与 Error 是两件事，文案也不一样
        Error,       // 失败，细节见 OnlineJobState::error
        Offline,     // 断网/接口不可达；本地曲库与播放照常
    };

    // 账号状态。扫码的四态（800/801/802/803）在这里被压成 Waiting/Scanned/Expired，
    // 厂商码值的映射只出现在 adapters 里一处。
    enum class AccountPhase : std::uint8_t
    {
        Guest,
        QrWaiting,
        QrScanned,
        QrExpired,
        LoggedIn,
        Expiring, // cookie 还在，但已接近失效（或某次请求刚被判未登录）
        Failed,
    };

    // 在线页的数据来源。`Search` 之外的类型是 R5 之后的事，但值先占好，
    // 免得以后加值时要动命令的二进制布局。
    enum class SourceKind : std::uint8_t
    {
        Search,
        Playlist,
        Favorites,
        Recent,
        DailyRecommend,
        Album,
    };

    [[nodiscard]] const char *toName(JobPhase phase) noexcept;
    [[nodiscard]] const char *toName(AccountPhase phase) noexcept;
    [[nodiscard]] const char *toName(SourceKind kind) noexcept;

    // 在线侧唯一的失败形态：分类沿用 Kernel 的 ErrorCode，不另造一套 ErrorKind。
    // `message` 已经是给人看的中文，UI 直接显示；`retryable` 决定按钮是"重试"
    // 还是"去登录"。
    struct OnlineError
    {
        kernel::ErrorCode code{kernel::ErrorCode::None};
        std::string message;
        bool retryable{false};
        std::int32_t retryAfterMs{0}; // 429 时上游建议的退避
    };

    // 一行在线结果。字段是"界面已经能直接用"的形态：
    // artists 已经连成 " / "，qualityLabel 已经是"极高/试听"这种中文。
    struct OnlineTrackDto
    {
        std::string songId;
        std::string title;
        std::string artists;
        std::string album;
        std::int64_t durationMs{0};
        std::string coverUrl;     // http(s) 封面地址，加载由界面层负责
        std::string qualityLabel; // "标准" / "较高" / "极高" / "试听"
        bool playable{false};     // false → 行尾灰标签"无版权"
        bool fromCache{false};
    };

    // 便宜的状态快照，界面可以 20Hz 拉它（不含行数据）。
    struct OnlineJobState
    {
        std::uint64_t token{0};
        JobPhase phase{JobPhase::Idle};
        std::int32_t total{0};
        std::int32_t loaded{0};
        OnlineError error;
        // 行数据变化时才 +1。界面据此决定"要不要 Fetch 一次行"，
        // 避免每次轮询都把整页 DTO 拷一遍。
        std::uint64_t revision{0};
    };

    // 行数据页。`token` 与 `revision` 都要回传给界面：
    // token 用来丢弃过期结果，revision 用来判断这一页是不是自己刚看到的那一版。
    struct OnlinePageDto
    {
        std::uint64_t token{0};
        std::uint64_t revision{0};
        std::vector<OnlineTrackDto> rows;
        bool hasMore{false};
    };

    // 账号胶囊要的一切。`nextRetryAtUnix` 非 0 且大于 now 时，界面显示"稍后再试"。
    struct SourceStatusDto
    {
        std::string provider{"ncm"};
        bool signedIn{false};
        AccountPhase accountPhase{AccountPhase::Guest};
        std::string nickname;
        std::int64_t nextRetryAtUnix{0};
        OnlineError lastError;
        bool enabled{true};
    };

    struct QrLoginDto
    {
        std::string provider{"ncm"};
        AccountPhase phase{AccountPhase::Guest};
        // 二维码矩阵：每行一个 "0"/"1" 字符串，1 = 黑格，界面逐格画出来。
        // 不走图片：明文接口不返回二维码图，自己编码成 PNG 又要引编码器与图片插件。
        std::vector<std::string> rows;
        // 每次「换一张码」+1：界面据此判断要不要重画，而不是每帧比对矩阵。
        std::uint64_t session{0};
        std::string message; // 给人看的一句话（"已过期，点刷新"）
    };

    struct PlaylistDto
    {
        std::string playlistId;
        std::string name;
        std::int32_t trackCount{0};
        std::string coverUrl;
    };

    // 一次解析出来的可播放直链。R3 只给表现层用：它把 url 塞进 QueueEntry.filePath，
    // 再连同 trackId=0 一起派发 PlayTracks —— 播放层不需要知道"在线"这回事。
    struct OnlinePlaybackEntry
    {
        std::string songId;
        std::string url;
        std::int32_t bitrateKbps{0};
    };

    // revision 变了才重建队列：表现层按它去重，避免每次轮询都把队列重装一遍。
    struct OnlinePlaybackDto
    {
        std::uint64_t revision{0};
        std::vector<OnlinePlaybackEntry> entries;
        std::size_t startIndex{0};
        OnlineError error;
    };

    // 降级矩阵（docs/ONLINE.md §5.3）的计数器：每出现一次就 +1，不允许"静默为空"。
    // 字段是**按用户可见的降级形态**分的，不是按 ErrorCode 分的——同一个 code 可能
    // 对应两种不同处置（比如 Unavailable 既可能是断网，也可能是接口 5xx）。
    struct DegradationCounters
    {
        std::int32_t offline{0};         // 连不上：DNS/TLS/连接重置
        std::int32_t timeout{0};         // 超时（含重试用尽）
        std::int32_t rateLimited{0};     // 429 / 风控提示
        std::int32_t unauthenticated{0}; // 登录失效或未登录访问私有接口
        std::int32_t upstream{0};        // 200 但结构不符——"接口可能已变更"
        std::int32_t urlExpired{0};      // 直链 403/404 后自动重解析
        std::int32_t cacheHits{0};       // 命中搜索缓存（不算降级，但同一个快照里看）
        std::int32_t totalErrors{0};     // 上面各项之和（含 cancelled 之类不单列的）
    };

    // 缓存现状。`bytes` 是 payload 的文本字节数，不查 sqlite 的页大小——
    // 目的是"证明上限在起效"，不是精确的磁盘占用。
    struct OnlineCacheStats
    {
        std::int32_t searchRows{0};
        std::int64_t searchBytes{0};
        std::int32_t songRows{0};
        std::int32_t detailRows{0};
        std::int32_t capRows{0};      // 上限常量，界面照实显示
        std::int64_t capBytes{0};
    };

    // 最近一次失败。`atUnix` 让界面能写"3 分钟前"，`where` 是唯一允许出现
    // 内部字符串的字段（"搜索" / "取地址" / "登录校验"），不含 URL 与 cookie。
    struct OnlineErrorRecord
    {
        std::int64_t atUnix{0};
        std::string where; // 见上
        OnlineError error;
    };

    // 一次性诊断快照。只在用户点开"诊断"时取，不进 50ms 轮询。
    struct OnlineDiagnosticsDto
    {
        std::string provider{"ncm"};
        DegradationCounters degradations;
        // 连续失败与退避闸门（policy::FailureCounter 的投影）。
        std::int32_t consecutiveFailures{0};
        std::int64_t nextRetryAtUnix{0};
        // 令牌桶余量（3 req/s、burst 6）。`fromCache` 的请求不取令牌。
        double tokensLeft{0.0};
        double tokenBurst{0.0};
        OnlineCacheStats cache;
        // 最新在前，最多 10 条。
        std::vector<OnlineErrorRecord> recentErrors;
    };

} // namespace sonora::online::contracts
