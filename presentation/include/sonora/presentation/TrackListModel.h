// 曲库列表的 ViewModel。
//
// 它只做三件事：把查询结果摆成 Qt 的行、把用户意图投进总线、把错误变成一句人话。
// 它**不持有**任何聚合，也不知道 SQLite 存在——这一层能被 QML 直接调用，
// 所以它必须是最薄的一层。
//
// 唯一的数据来源是两条总线：命令走 dispatch，查询走 ask。中间没有"服务对象"
// 可以让它绕过总线去找仓储，这正是 CQRS 通道分离在 MVVM 里的具体形态。
#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

#include <atomic>
#include <thread>
#include <vector>

#include "sonora/catalog/contracts/Commands.h"
#include "sonora/catalog/contracts/Queries.h"
#include "sonora/kernel/ICommandBus.h"
#include "sonora/kernel/IQueryBus.h"

namespace sonora::presentation
{

    class TrackListModel final : public QAbstractListModel
    {
        Q_OBJECT
        Q_PROPERTY(int count READ count NOTIFY countChanged)
        Q_PROPERTY(QString status READ status NOTIFY statusChanged)
        // 首次启动：既没记过曲库目录、库里也是一首都没有。QML 据此弹引导页。
        // 它只是**视图状态**——目录记在哪儿由宿主决定（见 host/main.cpp 的 QSettings），
        // 这一层不碰持久化。
        Q_PROPERTY(bool needsSetup READ needsSetup NOTIFY needsSetupChanged)

    public:
        enum Role
        {
            TitleRole = Qt::UserRole + 1,
            ArtistRole,
            AlbumRole,
            DurationRole,
            FilePathRole,
            CoverRole,
        };
        Q_ENUM(Role)

        TrackListModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                       QObject *parent = nullptr);

        [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

        // 分页：ListView 滚近末尾时自动调 fetchMore()，一次 200 行。
        [[nodiscard]] bool canFetchMore(const QModelIndex &parent) const override;
        void fetchMore(const QModelIndex &parent) override;

        // 库里一共多少首——不是已加载多少行。侧栏上那个数字是"我的曲库有多大"，
        // 不是"列表这会儿装了多少"；后者在 status 文本里（"共 N 首（已加载 M）"）。
        [[nodiscard]] int count() const { return static_cast<int>(total_count_); }
        // 已加载的行数。给需要区分的人用（状态栏、调试）。
        [[nodiscard]] int loadedCount() const { return static_cast<int>(rows_.size()); }
        [[nodiscard]] QString status() const { return status_; }

        // 重新跑一次列表查询。M1 是全量刷新；分页与增量留给 M5 的投影表。
        Q_INVOKABLE void refresh();

        // 导入一个文件夹。path 来自 QML（UTF-8），交给命令总线时也保持 UTF-8。
        Q_INVOKABLE void importFolder(const QString &path);

        // 首次启动引导页选中的曲库目录：导入它，并广播 folderChosen——
        // 宿主接住这个信号把它记进设置。**不在这里写 QSettings**：
        // 设置迟早属于 Preferences 模块，presentation 现在先别抢那块地。
        Q_INVOKABLE void chooseFolder(const QString &path);

        [[nodiscard]] bool needsSetup() const { return needs_setup_; }
        void setNeedsSetup(bool value);
        // 引导页上点"以后再说"。只是把这张卡收起来；下次启动还是会问。
        Q_INVOKABLE void dismissSetup();

        // 过滤：传空串即取消过滤。命中判定在 SQL 侧（FTS5），模型只负责重新取第一页。
        Q_INVOKABLE void setSearchText(const QString &text);
        [[nodiscard]] QString searchText() const { return search_text_; }

        // 双击第 row 行：把整个列表当成队列，从这一行开始播。
        // 「整个列表」而不是「这一首」——否则"下一首"无处可去。
        Q_INVOKABLE void activate(int row);

        // 拖进来的东西（M3 验收：拖一个 mp3 就出声）。
        // 规则：文件 → 导入它所在目录 → 找到它 → 从它开始播；文件夹 → 只导入。
        Q_INVOKABLE void dropUrls(const QVariantList &urls);

        // 界面语言变了：重查一次曲库，让列表与状态行换语言。
        void retranslate();
    signals:
        void countChanged();
        void statusChanged();
        void needsSetupChanged();
        // 引导页选了目录。宿主（host/main.cpp）负责记住它。
        void folderChosen(const QString &path);

        // 已经开始播放（或尝试播放）。组合根据此刷新 PlayerViewModel。
        void playbackStarted();

    private:
        void setStatus(QString text);
        void playFrom(int row);
        // 在已加载的行里按路径找下标；找不到返回 -1。路径比较忽略大小写（Windows）。
        [[nodiscard]] int rowOfPath(const QString &filePath) const;

        // 起一个后台线程把 folders 逐个导入；跑完回到 GUI 线程刷新列表。
        // firstFile 非空表示"用户拖的是文件"，导入完直接播它。
        void startImport(const QStringList &folders, const QString &firstFile);

        // 追加下一页。refresh() 与 fetchMore() 都走它。
        void fetchMoreRows();

        kernel::ICommandBus &commands_;
        kernel::IQueryBus &queries_;
        std::vector<catalog::contracts::TrackDto> rows_;
        QString status_;
        QString search_text_;
        // 库里总共有多少首（来自 TrackListResult::totalCount），不是已加载多少。
        std::int64_t total_count_ = 0;
        static constexpr std::int32_t kPageSize = 200;
        // 导入是后台线程跑的；同一时刻只允许一批，否则两个线程会抢同一个写事务。
        std::atomic<bool> busy_{false};
        bool needs_setup_ = false;
    };

} // namespace sonora::presentation
