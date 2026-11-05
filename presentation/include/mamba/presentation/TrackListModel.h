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
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <vector>

#include "mamba/catalog/contracts/Queries.h"
#include "mamba/kernel/ICommandBus.h"
#include "mamba/kernel/IQueryBus.h"

namespace mamba::presentation {

class TrackListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        DurationRole,
        FilePathRole,
    };
    Q_ENUM(Role)

    TrackListModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                   QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return static_cast<int>(rows_.size()); }
    [[nodiscard]] QString status() const { return status_; }

    // 重新跑一次列表查询。M1 是全量刷新；分页与增量留给 M5 的投影表。
    Q_INVOKABLE void refresh();

    // 导入一个文件夹。path 来自 QML（UTF-8），交给命令总线时也保持 UTF-8。
    Q_INVOKABLE void importFolder(const QString &path);

    // 双击第 row 行：把整个列表当成队列，从这一行开始播。
    // 「整个列表」而不是「这一首」——否则"下一首"无处可去。
    Q_INVOKABLE void activate(int row);

    // 拖进来的东西（M3 验收：拖一个 mp3 就出声）。
    // 规则：文件 → 导入它所在目录 → 找到它 → 从它开始播；文件夹 → 只导入。
    Q_INVOKABLE void dropUrls(const QVariantList &urls);

signals:
    void countChanged();
    void statusChanged();

    // 已经开始播放（或尝试播放）。组合根据此刷新 PlayerViewModel。
    void playbackStarted();

private:
    void setStatus(QString text);
    void playFrom(int row);
    // 在已加载的行里按路径找下标；找不到返回 -1。路径比较忽略大小写（Windows）。
    [[nodiscard]] int rowOfPath(const QString &filePath) const;

    kernel::ICommandBus &commands_;
    kernel::IQueryBus &queries_;
    std::vector<catalog::contracts::TrackDto> rows_;
    QString status_;
};

}  // namespace mamba::presentation
