// Sonora 组合根（composition root）。
//
// 整个工程里，只有这一个文件同时看得见 Adapters 和 Presentation ——
// 具体实现在这里被构造、注入，然后表现层才拿到它需要的接口。
// 组合根天然依赖一切，这正是它的职责，也是边界规则里唯一豁免的地方。
//
// 这里能读出来的架构事实：
//   * 连接、迁移、五个端口实现，全在这一处被 new 出来；
//   * 应用层 handler 只拿到**接口引用**，它们不知道 SQLite 存在；
//   * 表现层只拿到两条总线 + 一个单例，它连 TrackDto 是谁写的都不知道。
// 想换掉 SQLite、换成 TagLib、换成 FFmpeg，改的是这个文件的几行，不是模块内部。

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>
#include <qqml.h>

#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sonora/catalog/adapters/CatalogSchema.h"
#include "sonora/catalog/adapters/FileCoverStore.h"
#include "sonora/catalog/adapters/FileSystemScanner.h"
#include "sonora/catalog/adapters/SqliteCreditDirectory.h"
#include "sonora/catalog/adapters/SqlitePlaybackRecorder.h"
#include "sonora/catalog/adapters/SqliteTrackListReader.h"
#include "sonora/catalog/adapters/SqliteTrackRepository.h"
#include "sonora/catalog/adapters/TagLibMetadataReader.h"
#include "sonora/catalog/contracts/Commands.h"
#include "sonora/catalog/contracts/Queries.h"
#include "sonora/catalog/core/app/ImportFolderHandler.h"
#include "sonora/catalog/core/app/ListTracksHandler.h"
#include "sonora/contracts/PlaybackEvents.h"
#include "sonora/kernel/CommandBus.h"
#include "sonora/kernel/EventBus.h"
#include "sonora/kernel/IClock.h"
#include "sonora/kernel/ILogger.h"
#include "sonora/kernel/QueryBus.h"
#include "sonora/platform/storage/Migrator.h"
#include "sonora/platform/storage/SqliteConnection.h"
#include "sonora/platform/storage/SqliteUnitOfWork.h"
#include "sonora/platform/win/ForegroundWindow.h"
#include "sonora/platform/audio/FfmpegOutput.h"
#include "sonora/playback/core/app/PlaybackHandlers.h"
#include "sonora/playback/core/app/PlaybackService.h"
#include "sonora/playlists/adapters/PlaylistsSchema.h"
#include "sonora/playlists/adapters/SqlitePlaylistRepository.h"
#include "sonora/playlists/core/app/PlaylistHandlers.h"
#include "sonora/playlists/core/app/PlaylistService.h"
#include "sonora/preferences/adapters/PreferencesSchema.h"
#include "sonora/preferences/adapters/SqliteSettingsStore.h"
#include "sonora/preferences/core/app/PreferencesHandlers.h"
#include "sonora/preferences/core/app/PreferencesService.h"
#include "sonora/lyrics/adapters/LyricsSourceChain.h"
#include "sonora/lyrics/adapters/OnlineLyricsSource.h"
#include "sonora/lyrics/adapters/SidecarLyricsSource.h"
#include "sonora/lyrics/adapters/TagLibLyricsSource.h"
#include "sonora/lyrics/core/app/GetLyricsHandler.h"
#include "sonora/online/adapters/OnlineSchema.h"
#include "sonora/online/adapters/OnlineService.h"
#include "sonora/platform/win/DpapiSecretStore.h"
#include "sonora/platform/net/QtHttpTransport.h"
#include "sonora/platform/net/SerialExecutor.h"
#include "sonora/platform/net/SerialWorker.h"
#include "sonora/presentation/Appearance.h"
#include "sonora/presentation/LyricsModel.h"
#include "sonora/presentation/OnlineViewModel.h"
#include "sonora/presentation/PlaylistsViewModel.h"
#include "sonora/presentation/PlayerViewModel.h"
#include "sonora/presentation/TrackListModel.h"

namespace
{

    namespace catalog = sonora::catalog;
    namespace kernel = sonora::kernel;
    namespace storage = sonora::platform::storage;
    namespace playback = sonora::playback;
    namespace audio = sonora::platform::audio;
    namespace presentation = sonora::presentation;
    namespace lyrics = sonora::lyrics;
    namespace contracts = sonora::contracts;
    namespace preferences = sonora::preferences;
    namespace playlists = sonora::playlists;

    // 临时的控制台日志。M4 之前没有真正的日志后端，但"命令慢于 5ms 要有人喊一声"
    // 这件事从第一天就得成立，否则性能预算是一句废话。
    class ConsoleLogger final : public kernel::ILogger
    {
    public:
        [[nodiscard]] bool enabled(kernel::LogLevel level) const noexcept override
        {
            return static_cast<int>(level) >= static_cast<int>(kernel::LogLevel::Info);
        }

        void log(kernel::LogLevel level, std::string_view message) override
        {
            std::fprintf(stderr, "[%s] %.*s\n", kernel::toName(level),
                         static_cast<int>(message.size()), message.data());
        }
    };

    // 数据库放在系统的应用数据目录，而不是程序目录：程序目录可能只读，
    // 而且卸载/重装时用户曲库不该跟着消失。
    //
    // 但要**先真的写一个文件试试**：沙箱、组策略、漫游配置被重定向，都会让
    // 目录看起来存在却写不进去，而 SQLite 对此只会说一句 "unable to open
    // database file"，把人引向"路径拼错了"这种错误方向。
    bool directoryIsWritable(const QString &dir)
    {
        if (dir.isEmpty() || !QDir().mkpath(dir))
        {
            return false;
        }
        QFile probe{dir + QStringLiteral("/.write-probe")};
        if (!probe.open(QIODevice::WriteOnly))
        {
            return false;
        }
        probe.close();
        probe.remove();
        return true;
    }

    // 应用程序的落盘位置：曲库、封面、设置、单实例锁都在这一处。
    // AppLocalData 而不是 AppData：曲库几千首、封面一份几百 KB，
    // 漫游配置在多机登录时会被同步走，那是几十上百兆的"漫游文件"。
    // 退路是程序旁边的 data/：开发机上比 AppData 更不容易被拦。
    std::string dataDirectory()
    {
        const QStringList candidates = {
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
            QCoreApplication::applicationDirPath() + QStringLiteral("/data"),
        };

        for (const auto &dir : candidates)
        {
            if (directoryIsWritable(dir))
            {
                return dir.toUtf8().toStdString();
            }
        }
        return {}; // 一个都写不进去，让上层报错并退出
    }

    std::string databasePath()
    {
        const auto dir = dataDirectory();
        if (dir.empty())
        {
            return {};
        }
        const auto path = QDir(QString::fromStdString(dir)).filePath(QStringLiteral("sonora.sqlite"));
        std::fprintf(stderr, "[info] 曲库位置：%s\n", path.toUtf8().constData());
        return path.toUtf8().toStdString();
    }

    // 用户设置走 INI 文件而不是注册表：卸载/inno 清理时一并删掉，界面上少一处残留。
    QString settingsPath()
    {
        const auto dir = dataDirectory();
        if (dir.empty())
        {
            return {};
        }
        return QDir(QString::fromStdString(dir)).filePath(QStringLiteral("settings.ini"));
    }

} // namespace

int main(int argc, char *argv[])
{
    // 漫画风的细描边与网点在 125% / 150% 缩放下最容易糊。
    // 让 Qt 不再把缩放比取整到 1，交给系统 DPI 和 QML 的矢量绘制去处理。
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Sonora"));
    QGuiApplication::setOrganizationName(QStringLiteral("Sonora"));
    QGuiApplication::setApplicationVersion(QStringLiteral(SONORA_VERSION));
    // Windows 显示名称（任务栏悬停、Alt+Tab 列表）。可执行文件自己的图标在
    // assets/app.rc 里，这个是运行期窗口图标——两者都要有，缺一个就会出现
    // "exe 有图标但任务栏是白纸"或反过来的情况。
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Sonora Music"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/sonora.png")));

    // 打包脚本与 CI 的探针：`sonora.exe --version` 只回答"这份二进制自称几版"，
    // 不碰数据库、不建锁、不开窗。
    if (QCoreApplication::arguments().contains(QStringLiteral("--version")))
    {
        std::printf("Sonora Music %s\n", SONORA_VERSION);
        std::fflush(stdout);
        return 0;
    }

    ConsoleLogger logger;

    // ---- 单实例锁 ----
    // 两个进程同时打开同一个 SQLite，症状不是"多弹一个窗口"，而是随机一次导入
    // 失败、播放计数悄悄少一条——比重复窗口难查得多，所以进存储之前先锁。
    // 锁文件放在数据目录里；进程退出（包括崩溃）由 QLockFile 的 stale 检测回收。
    const auto dataDir = dataDirectory();
    const QString instanceLockPath = dataDir.empty()
        ? QString{}
        : QDir(QString::fromStdString(dataDir)).filePath(QStringLiteral("instance.lock"));

    QLockFile instanceLock{instanceLockPath};
    if (!instanceLockPath.isEmpty() && !instanceLock.tryLock(0))
    {
        // 已有实例：把那扇窗提到前台。找不到窗口就只是退出——宁可"没反应"，
        // 也不要弹一个"已经在运行"然后用户还得自己去任务栏找。
        if (!sonora::platform::win::raiseWindowByTitle(L"Sonora Music"))
        {
            std::fprintf(stderr, "[info] Sonora Music 已经在运行了\n");
        }
        return 0;
    }

    // ---- 存储 ----
    const auto dbPath = databasePath();
    if (dbPath.empty())
    {
        std::fprintf(stderr, "找不到可写的曲库目录\n");
        return 1;
    }

    auto opened = storage::SqliteConnection::open(dbPath);
    if (!opened)
    {
        std::fprintf(stderr, "打开数据库失败：%s\n", opened.error().message().c_str());
        return 1;
    }
    auto connection = std::move(opened).take();

    auto migrator = storage::Migrator{connection, catalog::adapters::kModuleName};
    if (auto migrated = migrator.apply(catalog::adapters::migrations()); !migrated)
    {
        std::fprintf(stderr, "数据库迁移失败：%s\n", migrated.error().message().c_str());
        return 1;
    }

    // 在线缓存是**独立模块的独立版本序列**：曲库升到 6 不影响它，反过来也一样。
    // 这就是 per-module __module_version 存在的理由（platform/storage/Migrator.h 顶部）。
    auto onlineMigrator = storage::Migrator{connection, sonora::online::adapters::kModuleName};
    if (auto migrated = onlineMigrator.apply(sonora::online::adapters::migrations()); !migrated)
    {
        std::fprintf(stderr, "在线缓存迁移失败：%s\n", migrated.error().message().c_str());
        return 1;
    }

    // 设置与歌单各有自己的版本序列。偏好表只有一张两列的 kv，歌单是聚合
    // （playlist + playlist_item）。两者都只加列/建表，不回填——R5 那次教训。
    auto preferencesMigrator =
        storage::Migrator{connection, sonora::preferences::adapters::kModuleName};
    if (auto migrated = preferencesMigrator.apply(sonora::preferences::adapters::migrations());
        !migrated)
    {
        std::fprintf(stderr, "设置迁移失败：%s\n", migrated.error().message().c_str());
        return 1;
    }

    auto playlistsMigrator = storage::Migrator{connection, sonora::playlists::adapters::kModuleName};
    if (auto migrated = playlistsMigrator.apply(sonora::playlists::adapters::migrations()); !migrated)
    {
        std::fprintf(stderr, "歌单迁移失败：%s\n", migrated.error().message().c_str());
        return 1;
    }

    // ---- 封面目录 ----
    // 与库文件同级：库在 <data>/sonora.sqlite，封面在 <data>/covers。
    // 放一起的理由是"要搬就一起搬"——把封面挪到临时目录，清一次缓存整个封面墙就空了；
    // 反过来把库也算进去，用户搬曲库时不会丢封面。
    const std::string coverRoot = [&dbPath]
    {
        const auto slash = dbPath.find_last_of("/\\");
        const std::string parent =
            slash == std::string::npos ? std::string{} : dbPath.substr(0, slash);
        return parent.empty() ? std::string{"covers"} : parent + "/covers";
    }();
    auto coverStore = std::make_shared<catalog::adapters::FileCoverStore>(coverRoot);

    // ---- 适配器（端口的具体实现）----
    auto creditDirectory = std::make_shared<catalog::adapters::SqliteCreditDirectory>(connection);
    auto trackRepository = std::make_shared<catalog::adapters::SqliteTrackRepository>(connection);
    auto trackListReader =
        std::make_shared<catalog::adapters::SqliteTrackListReader>(connection, coverRoot);
    auto unitOfWork = std::make_shared<storage::SqliteUnitOfWork>(connection);
    auto scanner = std::make_shared<catalog::adapters::FileSystemScanner>();
    // M5 起用 TagLib：真实时长 + 内嵌封面。BasicMetadataReader 留着，
    // 它是"端口可以被替换"的活证据（换实现，上层一行没动）。
    auto metadataReader = std::make_shared<catalog::adapters::TagLibMetadataReader>();

    // 全文索引与曲目表对齐。
    //
    // 迁移只建 track_fts（索引体要 C++ 算，SQL 造不出来），所以老库升级上来时
    // 索引是空的——表现是"列表里有、搜不到"。V5 之后索引体存在 tracks.search_body、
    // 由触发器同步，所以除了"行数对不上"，还要看"有多少行的 search_body 还是空串"
    // （V5 之前的老库升级上来就是这种状态：fts 里有旧数据，新列全是空）。
    // 两种不一致都走同一条补写路径；一致就直接跳过（正常启动零开销）。
    {
        const auto tracks = connection.queryInt("SELECT COUNT(*) FROM tracks");
        const auto indexed = connection.queryInt("SELECT COUNT(*) FROM track_fts");
        const auto unindexed = connection.queryInt(
            "SELECT COUNT(*) FROM tracks WHERE search_body = ''");
        const bool stale = tracks && indexed
                           && (tracks.value() != indexed.value()
                               || (unindexed && unindexed.value() > 0));
        if (stale)
        {
            if (auto rebuilt = trackRepository->rebuildSearchIndex(); rebuilt)
            {
                std::fprintf(stderr, "[info] 重建全文索引：%lld 行\n",
                             static_cast<long long>(rebuilt.value()));
            }
        }
    }

    kernel::SystemClock clock;

    // ---- 应用层（用例）----
    auto importHandler = std::make_shared<catalog::core::ImportFolderHandler>(
        *scanner, *metadataReader, *creditDirectory, *trackRepository, *unitOfWork, clock, &logger,
        coverStore.get());
    auto listHandler = std::make_shared<catalog::core::ListTracksHandler>(*trackListReader);

    // ---- 总线 ----
    kernel::CommandBus commands{&logger};
    kernel::QueryBus queries;

    commands.on<catalog::contracts::ImportFolder>(importHandler);
    queries.on<catalog::contracts::ListTracks, catalog::contracts::TrackListResult>(
        [listHandler](const catalog::contracts::ListTracks &query)
        { return listHandler->handle(query); });

    // ---- 播放 ----
    // audioOutput 同时实现 IAudioOutput（给领域用）与 IPositionStream（给界面用）。
    // 它只被这一层认识；PlaybackService 拿到的是引用，看不到 Qt。
    //
    // M3 用的是 QtMultimediaOutput（QMediaPlayer，格式支持看系统）。M4 换成
    // FfmpegOutput：解码自己来，输出走 QAudioSink + AudioRing。
    // 想退回旧引擎，把这一行换成 audio::QtMultimediaOutput 即可 —— 两个类实现的
    // 是同一组端口，其余代码一行都不用动。这正是 §2.5 依赖倒置要换到的东西。
    // ---- 跨模块事件 ----
    // M6 之前这里什么都没有：模块之间没有任何耦合，也就没有需要通知的事。
    // 现在 Playback 要告诉 Catalog"这首放完了"，而 Playback 不许直接调 Catalog 的仓储，
    // 于是有了第一条集成事件 TrackFinished 和它的唯一订阅者。
    kernel::EventBus events;
    catalog::adapters::SqlitePlaybackRecorder playbackRecorder{connection};
    events.subscribe<contracts::TrackFinished>(
        [&playbackRecorder, &logger](const contracts::TrackFinished &event)
        {
            if (auto recorded = playbackRecorder.record(event.trackId, event.playedAtUnix);
                !recorded)
            {
                // 曲目可能刚被删掉（边放边删是允许的），记不上不该让播放停下来。
                logger.warn("播放次数没记上（trackId={}）：{}", event.trackId,
                            recorded.error().message());
            }
        });

    // ---- 歌词：两个来源按顺序试，第一个有文本的赢 ----
    // 同名 .lrc 排在前面：它跟音频是同一个文件夹里的东西，改一行就能看到结果；
    // 内嵌标签排在后面当兜底。
    lyrics::adapters::SidecarLyricsSource sidecarLyrics;
    lyrics::adapters::TagLibLyricsSource embeddedLyrics;
    lyrics::adapters::LyricsSourceChain lyricsChain;
    lyricsChain.add(sidecarLyrics);
    lyricsChain.add(embeddedLyrics);
    // 第三来源：在线歌词（N3）。**排在最后**——本地有词就别去麻烦网络。
    // 它只依赖查询总线，在线服务在下面才构造：注册晚于构造没关系，
    // 真正 ask 发生在播放时（那时总线早就满了）。
    lyrics::adapters::OnlineLyricsSource onlineLyrics{queries};
    lyricsChain.add(onlineLyrics);
    lyrics::core::GetLyricsHandler lyricsHandler{lyricsChain};
    queries.on<lyrics::contracts::GetLyrics, lyrics::contracts::LyricsView>(
        [&lyricsHandler](const lyrics::contracts::GetLyrics &query)
        { return lyricsHandler.handle(query); });

    // ---- 在线音源 ----
    // R1 只到骨架：命令与查询都同步返回，网络留给 R2 的 `platform/net`。
    // 现在就接线的理由是**先让"命令 → 状态表 → 50ms 轮询 → 三态界面"这条链被真实走通**：
    // R2 换进去的只是"谁来填行"，总线、ViewModel、QML 一行都不用改。
    //
    // 在线曲目**不进曲库**（docs/ONLINE.md §1.3）：它们有自己的 id 空间与生命周期，
    // 混进 tracks 表会让"曲目必有本地路径"这条不变量永久失效。
    // ---- 在线：一条 worker 线程 + 一个传输 ----
    //
    // QNetworkAccessManager 只能在自己那条线程上构造和使用，所以传输对象由
    // worker 线程自己 new；主线程手里只有基类指针，通过 IHttpTransport 说话。
    // `netWorker` 必须比 `onlineService` 活得久（后者持有传输的引用），
    // 所以它在这里先声明，退出时最后销毁。
    sonora::platform::net::SerialWorker netWorker;
    sonora::platform::net::SerialExecutor netExecutor{netWorker};

    std::unique_ptr<sonora::online::contracts::IHttpTransport> httpTransport;
    {
        std::promise<void> created;
        auto ready = created.get_future();
        netWorker.post(
            [&httpTransport, &created]
            {
                httpTransport = std::make_unique<sonora::platform::net::QtHttpTransport>();
                created.set_value();
            });
        ready.wait();
    }

    // 凭据目录放在曲库旁边（`<AppData>/Sonora/Sonora/secrets`）：与数据库同生命周期，
    // 用户备份曲库时凭据跟着走，但密文只有本账户能解开（DPAPI）。
    const auto secretsDir = std::filesystem::path(dbPath).parent_path() / "secrets";
    sonora::platform::win::DpapiSecretStore secretStore{secretsDir};

    sonora::online::adapters::OnlineService onlineService{*httpTransport, netExecutor,
                                                          connection, secretStore,
                                                          clock};
    {
        using namespace sonora::online::contracts;
        commands.on<RequestOnlinePage>([&onlineService](const RequestOnlinePage &command)
                                       { return onlineService.handle(command); });
        commands.on<CancelOnlineJob>([&onlineService](const CancelOnlineJob &command)
                                     { return onlineService.handle(command); });
        commands.on<RetryOnlineJob>([&onlineService](const RetryOnlineJob &command)
                                    { return onlineService.handle(command); });
        commands.on<ResolveAndPlayOnline>([&onlineService](const ResolveAndPlayOnline &command)
                                          { return onlineService.handle(command); });
        commands.on<SignInWithCookie>([&onlineService](const SignInWithCookie &command)
                                      { return onlineService.handle(command); });
        commands.on<SignOut>([&onlineService](const SignOut &command)
                             { return onlineService.handle(command); });
        commands.on<StartQrLogin>([&onlineService](const StartQrLogin &command)
                                  { return onlineService.handle(command); });
        commands.on<CancelQrLogin>([&onlineService](const CancelQrLogin &command)
                                   { return onlineService.handle(command); });
        commands.on<LoadCollections>([&onlineService](const LoadCollections &command)
                                     { return onlineService.handle(command); });
        commands.on<LoadMyPlaylists>([&onlineService](const LoadMyPlaylists &command)
                                     { return onlineService.handle(command); });
        commands.on<ToggleFavorite>([&onlineService](const ToggleFavorite &command)
                                    { return onlineService.handle(command); });

        queries.on<GetOnlineJob, OnlineJobState>(
            [&onlineService](const GetOnlineJob &query)
            { return onlineService.handle(query); });
        queries.on<FetchOnlineRows, OnlinePageDto>(
            [&onlineService](const FetchOnlineRows &query)
            { return onlineService.handle(query); });
        queries.on<GetSourceStatus, SourceStatusDto>(
            [&onlineService](const GetSourceStatus &query)
            { return onlineService.handle(query); });
        queries.on<GetQrLoginState, QrLoginDto>(
            [&onlineService](const GetQrLoginState &query)
            { return onlineService.handle(query); });
        queries.on<GetMyPlaylists, std::vector<PlaylistDto>>(
            [&onlineService](const GetMyPlaylists &query)
            { return onlineService.handle(query); });
        queries.on<GetOnlinePlayback, OnlinePlaybackDto>(
            [&onlineService](const GetOnlinePlayback &query)
            { return onlineService.handle(query); });
        queries.on<GetOnlineDiagnostics, OnlineDiagnosticsDto>(
            [&onlineService](const GetOnlineDiagnostics &query)
            { return onlineService.handle(query); });
        queries.on<FetchCollections, CollectionPageDto>(
            [&onlineService](const FetchCollections &query)
            { return onlineService.handle(query); });
        queries.on<FetchCollectionDetail, CollectionDetailDto>(
            [&onlineService](const FetchCollectionDetail &query)
            { return onlineService.handle(query); });
        // 在线歌词（N3）：歌词模块通过它取词。查询是"取一次、拿一次"，
        // 没取回来时回 ready=false，表现层一秒后再问。
        queries.on<FetchOnlineLyrics, OnlineLyricDto>(
            [&onlineService](const FetchOnlineLyrics &query)
            { return onlineService.handle(query); });
    }

    // 查询都注册好了再恢复会话：回包要靠 GetSourceStatus 才能被界面读到，
    // 早于注册去问网易云，结果会落空。
    onlineService.restoreSession();

    // 打包冒烟要有一条能 grep 的"在线链路真的起来了"的证据：只看到窗口出现
    // 不能证明 Qt6Network 与 TLS 后端被正确部署（缺 tls/qschannelbackend.dll
    // 时窗口照常开，只是所有请求 SYSLIB 失败）。release.yml 的冒烟断言这行。
    std::fprintf(stderr, "[info] 在线音源就绪：transport=QtHttpTransport, provider=ncm\n");
    std::fflush(stderr);

    audio::FfmpegOutput audioOutput;
    playback::core::PlaybackService playbackService{audioOutput, &logger, &audioOutput, &events};
    playback::core::registerPlayback(commands, queries, playbackService);

    // ---- 设置与歌单 ----
    // 设置是所有模块的公共习惯（音量、播放模式、将来源设置），但不属于任何一个
    // 业务模块；歌单是核心域里的一个小聚合。两者都只通过总线被看见。
    preferences::adapters::SqliteSettingsStore settingsStore{connection};
    preferences::core::PreferencesService preferencesService{settingsStore, &logger};
    preferences::core::registerPreferences(commands, queries, preferencesService);

    // 音量与播放模式跨会话恢复：先直接调服务恢复（**在挂回调之前**，否则恢复
    // 动作自己会触发一次回写），再挂"变了就存"的口。播放模块不认识 Preferences，
    // 它只有一个 std::function。
    {
        const auto volumeText = preferencesService.getOrDefault("playback/volume", "100");
        int restoredVolume = 100;
        try {
            restoredVolume = std::stoi(volumeText);
        } catch (const std::exception &) {
            // 库里的值被人手改坏了：按默认 100% 走，不阻断启动。
            restoredVolume = 100;
        }
        (void)playbackService.setVolume(restoredVolume);

        const auto modeText = preferencesService.getOrDefault("playback/mode", "sequential");
        (void)playbackService.setMode(playback::contracts::modeFromName(modeText));

        playbackService.onSettingsChanged(
            [&preferencesService](std::int32_t volume, playback::contracts::PlaybackMode mode)
            {
                // 失败不打断播放：设置存不下比一首歌放不出来轻得多。
                (void)preferencesService.set("playback/volume", std::to_string(volume));
                (void)preferencesService.set("playback/mode",
                                             playback::contracts::toName(mode));
            });
    }

    playlists::adapters::SqlitePlaylistRepository playlistRepository{connection};
    playlists::core::PlaylistService playlistService{playlistRepository, &clock};
    playlists::core::registerPlaylists(commands, queries, playlistService);

    // ---- N4：私人 FM ----
    // 一批 FM 曲目解析好直链后，由这里翻成队列条目交给播放服务。
    // 播放模块不知道"FM"是什么，它只会在队列播到末尾时发布 QueueNeedsMore；
    // 我们收到就再要一批——这条环由组合根闭合，两边互不认识。
    {
        using namespace sonora::online::contracts;
        commands.on<StartPersonalFm>([&onlineService](const StartPersonalFm &command)
                                     { return onlineService.handle(command); });
        commands.on<DislikeFmSong>([&onlineService](const DislikeFmSong &command)
                                   { return onlineService.handle(command); });

        onlineService.setFmListener(
            [&commands](const std::vector<OnlineTrackDto> &tracks)
            {
                std::vector<playback::contracts::QueueEntry> entries;
                entries.reserve(tracks.size());
                for (const OnlineTrackDto &track : tracks)
                {
                    playback::contracts::QueueEntry entry;
                    entry.trackId = 0; // 在线曲目没有本地 id
                    entry.filePath = track.playUrl;
                    entry.title = track.title;
                    entry.artistName = track.artists;
                    entry.coverPath = track.coverUrl;
                    entry.onlineSongId = track.songId;
                    entries.push_back(std::move(entry));
                }
                if (entries.empty())
                {
                    return;
                }
                (void)commands.dispatch(
                    playback::contracts::AppendTracks{std::move(entries), true});
            });

        events.subscribe<sonora::contracts::QueueNeedsMore>(
            [&commands](const sonora::contracts::QueueNeedsMore &)
            {
                // 队列空了才喊饿，所以这里只补一小批；失败不重试，
                // 下一首播完还会再来要（FM 是背景服务，断了就安静地等）。
                (void)commands.dispatch(StartPersonalFm{3});
            });
    }

    // ---- 表现层 ----
    sonora::presentation::TrackListModel library{commands, queries};
    sonora::presentation::LyricsModel lyricsModel{queries};
    sonora::presentation::PlayerViewModel player{commands, queries, audioOutput};
    player.attachLyrics(&lyricsModel);
    // 在线页：自己的 ViewModel，自己的轮询计时器（50ms），与播放器互不相干。
    sonora::presentation::OnlineViewModel online{commands, queries};
    // 歌单页的模型：它自己不认曲库，靠 ListTracks 建索引把 trackId 翻成标题。
    sonora::presentation::PlaylistsViewModel playlistsModel{commands, queries};
    // 双击列表项后立刻刷新一次播放器条，否则要等下一个 500ms 轮询周期。
    QObject::connect(&library, &presentation::TrackListModel::playbackStarted, &player,
                     &presentation::PlayerViewModel::refresh);
    // 组合根是唯一知道"有哪些单例"的地方，QML 侧只认这两个名字。
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Library", &library);
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Player", &player);
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Lyrics", &lyricsModel);
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Online", &online);
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Playlists", &playlistsModel);
    // 外观（主题三态 + 语言）。它要在 engine 建好之前就装好翻译，
    // 否则第一帧是中文、下一帧才变英文，观感像闪一下。
    sonora::presentation::Appearance appearance{settingsPath()};
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Appearance", &appearance);

    // 切语言时引擎会把 QML 里的 qsTr 绑定重跑一遍；C++ 这边"算好存起来"的
    // 文案（播放状态、模式名、列表状态行、账号区）得自己重算，否则要重启才换语言。
    QObject::connect(&appearance, &sonora::presentation::Appearance::languageChanged, &player,
                     [&player, &library, &lyricsModel, &online, &playlistsModel]
                     {
                         player.retranslate();
                         library.retranslate();
                         lyricsModel.retranslate();
                         online.retranslate();
                         playlistsModel.retranslate();
                     });

    const auto arguments = QCoreApplication::arguments();
    // 只认第一个**不以 - 开头**的参数当文件夹。
    // Qt 自己会往 argv 里塞东西（-qmljsdebugger=... 是 QML 预览/调试器用的），
    // 早先这里是裸的 arguments.at(1)，Qt 的调试参数会被当成文件夹路径去扫。
    QString folderArgument;
    for (int i = 1; i < arguments.size(); ++i)
    {
        if (!arguments.at(i).startsWith(QLatin1Char('-')))
        {
            folderArgument = arguments.at(i);
            break;
        }
    }

    // 曲库目录的三个来源，优先级从高到低：
    //   1. 命令行参数（开发/试音常用：sonora.exe D:\Music）
    //   2. 上次记下的目录（引导页或上次的命令行参数）
    //   3. 都没有 → 弹引导页让用户选，而不是留一个空列表让他猜
    //
    // 这个键暂时存在 QSettings 里（M9 的临时落脚点）：Preferences 模块至今只有
    // probe 空壳，"记住用户选的文件夹"不能等它。等 Preferences 真能落盘，搬过去，
    // 键名保持 library/root 以便迁移。
    // 用 INI 而不是默认的注册表：卸载时能连文件一起删干净，也能直接看/改。
    QSettings settings{settingsPath(), QSettings::IniFormat};
    const QString savedRoot = settings.value(QStringLiteral("library/root")).toString();

    QObject::connect(&library, &presentation::TrackListModel::folderChosen, &library,
                     [](const QString &path)
                     {
                         QSettings store{settingsPath(), QSettings::IniFormat};
                         store.setValue(QStringLiteral("library/root"), path);
                     });

    if (!folderArgument.isEmpty())
    {
        // 命令行给了目录：导入并记住。下次不带参数启动直接回到这里。
        library.importFolder(folderArgument);
        settings.setValue(QStringLiteral("library/root"), folderArgument);
    }
    else if (!savedRoot.isEmpty() && QDir(savedRoot).exists())
    {
        // 增量扫描：没变的文件连标签都不读，一万首二扫 400ms 左右。
        library.importFolder(savedRoot);
    }
    else
    {
        // 首次启动，或者记着的目录被删/被改名。refresh() 先把已有库铺出来，
        // 引导页盖在上面——用户选完目录，它自己会关。
        library.refresh();
        library.setNeedsSetup(true);
    }

    QQmlApplicationEngine engine;

    // QML 的类型/属性错误默认只走 qWarning，而 Windows 上的 GUI 子系统进程
    // 没有控制台时它就这么没了——表现是"窗口不出来、什么都不说"。这里显式打出来。
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        &app,
        [](const QList<QQmlError> &warnings)
        {
            for (const auto &warning : warnings)
            {
                std::fprintf(stderr, "[qml] %s\n", warning.toString().toUtf8().constData());
            }
        });

    // 切语言时让引擎重跑所有 qsTr 绑定（Qt 6.5+ 的 retranslate，不用重启）。
    appearance.setEngine(&engine);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []()
        { QCoreApplication::exit(1); },
        Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
    {
        std::fprintf(stderr, "[fatal] QML 加载失败，见上面 [qml] 行\n");
        return -1;
    }

    const int exitCode = app.exec();

    // transport 的析构也必须在它自己那条线程上：先投一个重置任务，再让 worker 排空并退出。
    // 顺序反过来的话，QNetworkAccessManager 会在主线程被 delete，而它的孩子在 worker 线程。
    netWorker.post([&httpTransport]
                   { httpTransport.reset(); });
    netWorker.stop();
    return exitCode;
}
