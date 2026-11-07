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
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>
#include <qqml.h>

#include <cstdio>
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
#include "sonora/platform/audio/FfmpegOutput.h"
#include "sonora/playback/core/app/PlaybackHandlers.h"
#include "sonora/playback/core/app/PlaybackService.h"
#include "sonora/lyrics/adapters/LyricsSourceChain.h"
#include "sonora/lyrics/adapters/SidecarLyricsSource.h"
#include "sonora/lyrics/adapters/TagLibLyricsSource.h"
#include "sonora/lyrics/core/app/GetLyricsHandler.h"
#include "sonora/online/adapters/OnlineSchema.h"
#include "sonora/online/adapters/OnlineService.h"
#include "sonora/presentation/LyricsModel.h"
#include "sonora/presentation/OnlineViewModel.h"
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

    std::string databasePath()
    {
        const QStringList candidates = {
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
            // 退路：程序旁边的 data/。开发机上比 AppData 更不容易被拦。
            QCoreApplication::applicationDirPath() + QStringLiteral("/data"),
        };

        for (const auto &dir : candidates)
        {
            if (!directoryIsWritable(dir))
            {
                continue;
            }
            const auto path = dir + QStringLiteral("/sonora.sqlite");
            std::fprintf(stderr, "[info] 曲库位置：%s\n", path.toUtf8().constData());
            return path.toUtf8().toStdString();
        }
        return {}; // 一个都写不进去，让上层报错并退出
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
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    // Windows 显示名称（任务栏悬停、Alt+Tab 列表）。可执行文件自己的图标在
    // assets/app.rc 里，这个是运行期窗口图标——两者都要有，缺一个就会出现
    // "exe 有图标但任务栏是白纸"或反过来的情况。
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Sonora Music"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/sonora.png")));

    ConsoleLogger logger;

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
    // 索引是空的——表现是"列表里有、搜不到"。这里比对行数补一次，
    // 一致就直接跳过（正常启动零开销）。
    {
        const auto tracks = connection.queryInt("SELECT COUNT(*) FROM tracks");
        const auto indexed = connection.queryInt("SELECT COUNT(*) FROM track_fts");
        if (tracks && indexed && tracks.value() != indexed.value())
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
    sonora::online::adapters::OnlineService onlineService;
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

        queries.on<GetOnlineJob, OnlineJobState>(
            [&onlineService](const GetOnlineJob &query) { return onlineService.handle(query); });
        queries.on<FetchOnlineRows, OnlinePageDto>(
            [&onlineService](const FetchOnlineRows &query) { return onlineService.handle(query); });
        queries.on<GetSourceStatus, SourceStatusDto>(
            [&onlineService](const GetSourceStatus &query) { return onlineService.handle(query); });
        queries.on<GetQrLoginState, QrLoginDto>(
            [&onlineService](const GetQrLoginState &query) { return onlineService.handle(query); });
        queries.on<GetMyPlaylists, std::vector<PlaylistDto>>(
            [&onlineService](const GetMyPlaylists &query) { return onlineService.handle(query); });
    }

    audio::FfmpegOutput audioOutput;
    playback::core::PlaybackService playbackService{audioOutput, &logger, &audioOutput, &events};
    playback::core::registerPlayback(commands, queries, playbackService);

    // ---- 表现层 ----
    sonora::presentation::TrackListModel library{commands, queries};
    sonora::presentation::LyricsModel lyricsModel{queries};
    sonora::presentation::PlayerViewModel player{commands, queries, audioOutput};
    player.attachLyrics(&lyricsModel);
    // 在线页：自己的 ViewModel，自己的轮询计时器（50ms），与播放器互不相干。
    sonora::presentation::OnlineViewModel online{commands, queries};
    // 双击列表项后立刻刷新一次播放器条，否则要等下一个 500ms 轮询周期。
    QObject::connect(&library, &presentation::TrackListModel::playbackStarted, &player,
                     &presentation::PlayerViewModel::refresh);
    // 组合根是唯一知道"有哪些单例"的地方，QML 侧只认这两个名字。
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Library", &library);
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Player", &player);
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Lyrics", &lyricsModel);
    qmlRegisterSingletonInstance("Sonora.Presentation", 1, 0, "Online", &online);

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

    if (!folderArgument.isEmpty())
    {
        // 命令行给一个文件夹就导入它。M3 会把这块换成拖放 + 文件选择器，
        // 但"能从一个具体路径跑通整条链"这件事，在 M1 是用来看的。
        library.importFolder(folderArgument);
    }
    else
    {
        library.refresh();
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

    return app.exec();
}
