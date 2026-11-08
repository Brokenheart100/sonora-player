#include "sonora/presentation/TrackListModel.h"

#include <QCoreApplication>

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <utility>

#include "sonora/catalog/contracts/Commands.h"
#include "sonora/playback/contracts/Commands.h"

namespace sonora::presentation
{
    namespace
    {

        // kernel 的错误消息是 UTF-8 的 std::string；QString 的构造默认按 Latin-1 解释，
        // 直接 QString(message.c_str()) 会把中文错误变成问号。
        QString fromUtf8(const std::string &text)
        {
            return QString::fromStdString(text);
        }

        // 毫秒 → "3:45"。放这里而不是 QML，是因为时长格式化属于"数据怎么显示"的一部分，
        // 而 QML 里写 JS 时间运算既慢又容易在 59.6 秒这种边上出岔子。
        QString formatDuration(qint64 milliseconds)
        {
            if (milliseconds <= 0)
            {
                return QStringLiteral("--:--");
            }
            const auto totalSeconds = milliseconds / 1000;
            const auto minutes = totalSeconds / 60;
            const auto seconds = totalSeconds % 60;
            return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
        }

    } // namespace

    TrackListModel::TrackListModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                                   QObject *parent)
        : QAbstractListModel(parent), commands_(commands), queries_(queries)
    {
    }

    int TrackListModel::rowCount(const QModelIndex &parent) const
    {
        if (parent.isValid())
        {
            return 0; // 列表模型没有树形结构
        }
        return static_cast<int>(rows_.size());
    }

    namespace
{
    // 曲库里的"未知艺术家/未知专辑"是导入时写进数据库的哨兵值
    // （Catalog.Core 零 Qt，不该产出界面文案）。显示时按语言换成占位符：
    // 中文下与库里逐字相同，英文下走翻译。空串同样落到占位符。
    constexpr char kNoArtist[] = "未知艺术家";
    constexpr char kNoAlbum[] = "未知专辑";

    QString displayArtist(const std::string &value)
    {
        if (value.empty() || value == kNoArtist)
        {
            return QCoreApplication::translate("TrackListModel", "未知艺术家");
        }
        return QString::fromStdString(value);
    }

    QString displayAlbum(const std::string &value)
    {
        if (value.empty() || value == kNoAlbum)
        {
            return QCoreApplication::translate("TrackListModel", "未知专辑");
        }
        return QString::fromStdString(value);
    }
} // namespace

QVariant TrackListModel::data(const QModelIndex &index, int role) const
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size()))
        {
            return {};
        }
        const auto &row = rows_[static_cast<std::size_t>(index.row())];
        switch (role)
        {
        case TitleRole:
            return QString::fromStdString(row.title);
        case ArtistRole:
            return displayArtist(row.artistName);
        case AlbumRole:
            return displayAlbum(row.albumTitle);
        case DurationRole:
            return formatDuration(static_cast<qint64>(row.durationMs));
        case FilePathRole:
            return QString::fromStdString(row.filePath);
        case CoverRole:
            // 给 QML 一个 file:// URL 而不是裸路径：Image.source 拿到裸路径会
            // 当相对地址去解析（相对当前工作目录），表现为"封面全都不显示"。
            if (row.coverPath.empty())
            {
                return QString{};
            }
            return QUrl::fromLocalFile(QString::fromStdString(row.coverPath)).toString();
        default:
            return {};
        }
    }

    QHash<int, QByteArray> TrackListModel::roleNames() const
    {
        // 名字小写开头：QML 里直接 model.title，不用想 Role 枚举。
        return {
            {TitleRole, "title"},
            {ArtistRole, "artist"},
            {AlbumRole, "album"},
            {DurationRole, "duration"},
            {FilePathRole, "filePath"},
            {CoverRole, "cover"},
        };
    }

    void TrackListModel::setStatus(QString text)
    {
        if (status_ == text)
        {
            return;
        }
        status_ = std::move(text);
        emit statusChanged();
    }

    void TrackListModel::refresh()
    {
        beginResetModel();
        rows_.clear();
        total_count_ = 0;
        endResetModel();
        emit countChanged();

        setStatus(tr("正在读取…"));
        fetchMoreRows();
    }

    bool TrackListModel::canFetchMore(const QModelIndex &parent) const
    {
        if (parent.isValid())
        {
            return false;
        }
        // total_count_ 只有第一页回来后才知道；还没拿到时按"可能还有"处理。
        return rows_.empty() || static_cast<std::int64_t>(rows_.size()) < total_count_;
    }

    void TrackListModel::fetchMore(const QModelIndex &parent)
    {
        if (parent.isValid())
        {
            return;
        }
        fetchMoreRows();
    }

    void TrackListModel::fetchMoreRows()
    {
        // 按窗口取：滚到哪里取到哪里。
        //
        // 一万首一次性取回来是几十毫秒的同步查询 + 十几 MB 的 DTO，
        // 而人一次只看得到十几行。QML 的 ListView 在滚近末尾时会自动调
        // fetchMore()，所以"分页"对用户是透明的——他只会觉得滚动没有尽头。
        catalog::contracts::ListTracks query;
        query.offset = static_cast<std::int32_t>(rows_.size());
        query.limit = kPageSize;
        query.searchText = search_text_.toUtf8().toStdString();

        const auto page = queries_.ask<catalog::contracts::TrackListResult>(query);
        if (!page)
        {
            setStatus(fromUtf8(page.error().message()));
            return;
        }

        const auto &result = page.value();
        // totalCount 每次都要更新：列表可能在两次翻页之间被导入改过。
        total_count_ = result.totalCount;

        if (!result.rows.empty())
        {
            const auto first = static_cast<int>(rows_.size());
            const auto last = first + static_cast<int>(result.rows.size()) - 1;
            beginInsertRows(QModelIndex(), first, last);
            rows_.insert(rows_.end(), result.rows.begin(), result.rows.end());
            endInsertRows();
            emit countChanged();
        }

        const auto loaded = static_cast<qlonglong>(rows_.size());
        setStatus(loaded < static_cast<qlonglong>(total_count_)
                      ? tr("共 %1 首（已加载 %2）").arg(static_cast<qlonglong>(total_count_)).arg(loaded)
                      : tr("共 %1 首").arg(static_cast<qlonglong>(total_count_)));
    }

    void TrackListModel::setSearchText(const QString &text)
    {
        if (search_text_ == text)
        {
            return;
        }
        search_text_ = text;
        refresh();
    }

    void TrackListModel::importFolder(const QString &path)
    {
        if (path.trimmed().isEmpty())
        {
            setStatus(tr("没有选择文件夹"));
            return;
        }
        startImport(QStringList{path}, QString{});
    }

    void TrackListModel::setNeedsSetup(bool value)
    {
        if (needs_setup_ == value)
        {
            return;
        }
        needs_setup_ = value;
        emit needsSetupChanged();
    }

    void TrackListModel::dismissSetup()
    {
        setNeedsSetup(false);
    }

    void TrackListModel::chooseFolder(const QString &path)
    {
        // 先记下路径再导入：导入是后台线程，失败时用户至少知道自己选的是哪儿。
        // 记的是"用户选过的目录"，与导入是否扫出歌曲无关——空文件夹也是他的选择，
        // 下次启动不该再弹一次引导页。
        emit folderChosen(path);
        setNeedsSetup(false);
        importFolder(path);
    }

    void TrackListModel::startImport(const QStringList &folders, const QString &firstFile)
    {
        // 一次只跑一批。
        //
        // 为什么不是"排个队慢慢跑"：导入期间用户可能又拖进来一个文件夹，
        // 排队意味着他得等前一批结束才看到任何反馈。直接拒绝并说明原因，
        // 比静默积压好。
        if (busy_.exchange(true))
        {
            setStatus(tr("上一批还在导入，等它跑完"));
            return;
        }

        setStatus(tr("正在导入…（%1 个文件夹）").arg(folders.size()));

        // 命令离线执行：扫描一万个文件、读一万次标签是几十秒的活，
        // 放在 GUI 线程上就是"界面白屏"。这条线是本项目"命令同步 < 5ms"
        // 那条规矩的落地方式——慢活在调用方挪走，而不是让 Handler 变异步。
        std::thread([self = QPointer<TrackListModel>(this), folders = QStringList{folders},
                     firstFile = QString{firstFile}, commands = &commands_]()
                    {
                        std::string failure;

                        for (const auto &folder : folders)
                        {
                            catalog::contracts::ImportFolder command;
                            command.folderPath = folder.toUtf8().toStdString();
                            // 走总线只拿得到"成功/失败"：dispatch 是类型擦除的，
                            // 返回 Result<void>，ImportSummary 出不了这条通道。
                            // 逐文件计数留在 Handler 的日志里——那是运维信息，
                            // 界面只需要"完事了，多少首"（刷新后的总数）。
                            if (auto result = commands->dispatch(command); !result)
                            {
                                failure = result.error().message();
                            }
                        }

                        // 回到 GUI 线程再碰模型：QAbstractListModel 不是线程安全的。
                        QMetaObject::invokeMethod(
                            self, [self, failure, firstFile]()
                            {
                                if (self.isNull())
                                {
                                    return;
                                }
                                self->busy_.store(false);
                                // 刷新会把状态改成"共 N 首"——导入刚结束，用户要看的是这个数。
                                self->refresh();
                                if (!failure.empty())
                                {
                                    self->setStatus(fromUtf8(failure));
                                    return;
                                }
                                if (!firstFile.isEmpty())
                                {
                                    const auto row = self->rowOfPath(firstFile);
                                    if (row >= 0)
                                    {
                                        self->playFrom(row);
                                    }
                                }
                            },
                            Qt::QueuedConnection);
                    }).detach();
    }

    void TrackListModel::activate(int row)
    {
        playFrom(row);
    }

    void TrackListModel::playFrom(int row)
    {
        if (row < 0 || row >= static_cast<int>(rows_.size()))
        {
            return;
        }

        // 整个列表进队列，从这一行开始。DTO 是扁平的，直接搬进 QueueEntry 即可——
        // 表现层不需要知道 PlayQueue 里有什么，它只管把用户点的那一行交出去。
        playback::contracts::PlayTracks command;
        command.entries.reserve(rows_.size());
        for (const auto &item : rows_)
        {
            command.entries.push_back(playback::contracts::QueueEntry{
                item.id, item.filePath, item.title, item.artistName, item.coverPath});
        }
        command.startIndex = row;

        const auto result = commands_.dispatch(command);
        if (!result)
        {
            setStatus(fromUtf8(result.error().message()));
            return;
        }
        setStatus(tr("正在播放：%1")
                      .arg(QString::fromStdString(rows_[static_cast<std::size_t>(row)].title)));
        emit playbackStarted();
    }

    void TrackListModel::dropUrls(const QVariantList &urls)
    {
        QStringList folders;
        QString firstFile;

        for (const auto &value : urls)
        {
            const QUrl url(value.toString());
            const auto local = url.toLocalFile();
            if (local.isEmpty())
            {
                continue;
            }
            const QFileInfo info(local);
            if (info.isDir())
            {
                folders << info.absoluteFilePath();
            }
            else if (info.isFile())
            {
                folders << info.absolutePath();
                if (firstFile.isEmpty())
                {
                    firstFile = info.absoluteFilePath();
                }
            }
        }

        if (folders.isEmpty())
        {
            setStatus(tr("拖进来的东西没有文件，也没有文件夹"));
            return;
        }

        folders.removeDuplicates();

        // 拖的是文件：导入完直接播它，不要求用户再双击一次。
        // 拖的是文件夹：只导入——用户没说想听哪首，替他挑一首是越权。
        startImport(folders, firstFile);
    }

    int TrackListModel::rowOfPath(const QString &filePath) const
    {
        // Windows 路径大小写不敏感，分隔符可能是 / 或 \：两边都规整成 / 再比。
        const auto normalized = QDir::fromNativeSeparators(filePath);
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            const auto candidate =
                QDir::fromNativeSeparators(QString::fromStdString(rows_[i].filePath));
            if (candidate.compare(normalized, Qt::CaseInsensitive) == 0)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

} // namespace sonora::presentation
