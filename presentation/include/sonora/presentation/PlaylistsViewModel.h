// 歌单页的 ViewModel。
//
// 它跟曲库页是同一种东西：只把查询结果摆成 Qt 的行、把用户意图投进总线。
// 有一个地方不一样——**歌单条目只有 trackId**（Playlists 模块不认识曲库，
// 见 Playlists.h 里那段注释），而界面要显示标题、封面、能不能播。
// 所以这一层自己拿 `ListTracks` 建一张 id → TrackDto 的索引，
// 缺曲目的条目显示成"不可用占位"，这是数据边界换来的代价，不是设计遗漏。
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <vector>

#include "sonora/catalog/contracts/TrackDto.h"
#include "sonora/kernel/ICommandBus.h"
#include "sonora/kernel/IQueryBus.h"
#include "sonora/playlists/contracts/Playlists.h"

namespace sonora::presentation
{

    class PlaylistsViewModel final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
        Q_PROPERTY(int count READ count NOTIFY playlistsChanged)
        Q_PROPERTY(QString status READ status NOTIFY statusChanged)
        // 打开的是哪一个。QML 用一个页面切换（列表 ↔ 详情），不引第二套路由。
        Q_PROPERTY(bool opened READ opened NOTIFY openedChanged)
        Q_PROPERTY(QString currentName READ currentName NOTIFY openedChanged)
        Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)

    public:
        PlaylistsViewModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                           QObject *parent = nullptr);

        [[nodiscard]] QVariantList playlists() const { return playlists_; }
        [[nodiscard]] int count() const { return static_cast<int>(playlists_.size()); }
        [[nodiscard]] QString status() const { return status_; }
        [[nodiscard]] bool opened() const { return current_id_ != 0; }
        [[nodiscard]] QString currentName() const { return current_name_; }
        [[nodiscard]] QVariantList entries() const { return entries_; }

        Q_INVOKABLE void refresh();
        Q_INVOKABLE void createPlaylist(const QString &name);
        Q_INVOKABLE void removePlaylist(qint64 playlistId);
        Q_INVOKABLE void openPlaylist(qint64 playlistId, const QString &name);
        Q_INVOKABLE void closePlaylist();
        Q_INVOKABLE void removeTrack(qint64 trackId);
        // 双击第 index 行：把歌单当成队列，从这一首开始播。
        Q_INVOKABLE void playEntry(int index);
        Q_INVOKABLE void playAll();
        // 从曲库第 row 行加一首进当前歌单。曲库有多少行这一层不缓存，
        // 所以按要哪一行直接问一页（limit=1），省一次整表投影。
        Q_INVOKABLE void addFromLibrary(int row);

    signals:
        void playlistsChanged();
        void statusChanged();
        void openedChanged();
        void entriesChanged();

    private:
        void setStatus(QString text);
        void reloadEntries();
        // 懒加载曲库索引：第一次需要显示条目时才拉，且一次拉完（投影 SQL 很快）。
        void ensureTrackIndex();
        // 把最近一次拉到的条目按"能不能播"翻译成界面行。
        void publishEntries();

        kernel::ICommandBus &commands_;
        kernel::IQueryBus &queries_;

        QHash<qint64, catalog::contracts::TrackDto> index_;
        bool index_ready_ = false;

        std::int64_t current_id_ = 0;
        QString current_name_;
        std::vector<playlists::contracts::PlaylistEntryDto> raw_entries_;

        QVariantList playlists_;
        QVariantList entries_;
        QString status_;
    };

} // namespace sonora::presentation
