// MambaMusic 组合根（composition root）。
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

#include "mamba/catalog/adapters/BasicMetadataReader.h"
#include "mamba/catalog/adapters/CatalogSchema.h"
#include "mamba/catalog/adapters/FileSystemScanner.h"
#include "mamba/catalog/adapters/SqliteCreditDirectory.h"
#include "mamba/catalog/adapters/SqliteTrackListReader.h"
#include "mamba/catalog/adapters/SqliteTrackRepository.h"
#include "mamba/catalog/contracts/Commands.h"
#include "mamba/catalog/contracts/Queries.h"
#include "mamba/catalog/core/app/ImportFolderHandler.h"
#include "mamba/catalog/core/app/ListTracksHandler.h"
#include "mamba/kernel/CommandBus.h"
#include "mamba/kernel/IClock.h"
#include "mamba/kernel/ILogger.h"
#include "mamba/kernel/QueryBus.h"
#include "mamba/platform/storage/Migrator.h"
#include "mamba/platform/storage/SqliteConnection.h"
#include "mamba/platform/storage/SqliteUnitOfWork.h"
#include "mamba/platform/audio/QtMultimediaOutput.h"
#include "mamba/playback/core/app/PlaybackHandlers.h"
#include "mamba/playback/core/app/PlaybackService.h"
#include "mamba/presentation/PlayerViewModel.h"
#include "mamba/presentation/TrackListModel.h"

namespace
{

    namespace catalog = mamba::catalog;
    namespace kernel = mamba::kernel;
    namespace storage = mamba::platform::storage;
    namespace playback = mamba::playback;
    namespace audio = mamba::platform::audio;
    namespace presentation = mamba::presentation;

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
            const auto path = dir + QStringLiteral("/mamba.sqlite");
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
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("MambaMusic"));
    QGuiApplication::setOrganizationName(QStringLiteral("MambaMusic"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));

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

    // ---- 适配器（端口的具体实现）----
    auto creditDirectory = std::make_shared<catalog::adapters::SqliteCreditDirectory>(connection);
    auto trackRepository = std::make_shared<catalog::adapters::SqliteTrackRepository>(connection);
    auto trackListReader = std::make_shared<catalog::adapters::SqliteTrackListReader>(connection);
    auto unitOfWork = std::make_shared<storage::SqliteUnitOfWork>(connection);
    auto scanner = std::make_shared<catalog::adapters::FileSystemScanner>();
    auto metadataReader = std::make_shared<catalog::adapters::BasicMetadataReader>();

    kernel::SystemClock clock;

    // ---- 应用层（用例）----
    auto importHandler = std::make_shared<catalog::core::ImportFolderHandler>(
        *scanner, *metadataReader, *creditDirectory, *trackRepository, *unitOfWork, clock, &logger);
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
    audio::QtMultimediaOutput audioOutput;
    playback::core::PlaybackService playbackService{audioOutput, &logger};
    playback::core::registerPlayback(commands, queries, playbackService);

    // ---- 表现层 ----
    mamba::presentation::TrackListModel library{commands, queries};
    mamba::presentation::PlayerViewModel player{commands, queries, audioOutput};
    // 双击列表项后立刻刷新一次播放器条，否则要等下一个 500ms 轮询周期。
    QObject::connect(&library, &presentation::TrackListModel::playbackStarted, &player,
                     &presentation::PlayerViewModel::refresh);
    // 组合根是唯一知道"有哪些单例"的地方，QML 侧只认这两个名字。
    qmlRegisterSingletonInstance("MambaMusic", 1, 0, "Library", &library);
    qmlRegisterSingletonInstance("MambaMusic", 1, 0, "Player", &player);

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
