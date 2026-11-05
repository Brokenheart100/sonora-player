#include "mamba/presentation/TrackListModel.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <utility>

#include "mamba/catalog/contracts/Commands.h"
#include "mamba/playback/contracts/Commands.h"

namespace mamba::presentation
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
            return QString::fromStdString(row.artistName);
        case AlbumRole:
            return QString::fromStdString(row.albumTitle);
        case DurationRole:
            return formatDuration(static_cast<qint64>(row.durationMs));
        case FilePathRole:
            return QString::fromStdString(row.filePath);
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
        catalog::contracts::ListTracks query;
        // M1 一次取 1000 行：这是"列表还能不能滚"的实验，不是分页设计。
        // M5 会换成按窗口取（滚动到哪里取到哪里），接口已经是 offset/limit 了。
        query.limit = 1000;

        const auto page = queries_.ask<catalog::contracts::TrackListResult>(query);

        beginResetModel();
        if (page)
        {
            rows_ = page.value().rows;
            setStatus(QStringLiteral("共 %1 首").arg(static_cast<qlonglong>(page.value().totalCount)));
        }
        else
        {
            rows_.clear();
            setStatus(fromUtf8(page.error().message()));
        }
        endResetModel();

        emit countChanged();
    }

    void TrackListModel::importFolder(const QString &path)
    {
        if (path.trimmed().isEmpty())
        {
            setStatus(QStringLiteral("没有选择文件夹"));
            return;
        }

        catalog::contracts::ImportFolder command;
        command.folderPath = path.toUtf8().toStdString();

        const auto result = commands_.dispatch(command);
        if (!result)
        {
            setStatus(fromUtf8(result.error().message()));
            return;
        }
        // 导入写完再刷新：读侧看到的一定是已提交的数据（事务先提交，再刷新列表）。
        refresh();
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
                item.id, item.filePath, item.title, item.artistName});
        }
        command.startIndex = row;

        const auto result = commands_.dispatch(command);
        if (!result)
        {
            setStatus(fromUtf8(result.error().message()));
            return;
        }
        setStatus(QStringLiteral("正在播放：%1")
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
            setStatus(QStringLiteral("拖进来的东西没有文件，也没有文件夹"));
            return;
        }

        folders.removeDuplicates();
        for (const auto &folder : folders)
        {
            importFolder(folder);
        }

        // 拖的是文件：导入完直接播它，不要求用户再双击一次。
        // 拖的是文件夹：只导入——用户没说想听哪首，替他挑一首是越权。
        if (!firstFile.isEmpty())
        {
            const auto row = rowOfPath(firstFile);
            if (row >= 0)
            {
                playFrom(row);
            }
            else
            {
                setStatus(QStringLiteral("已导入，但这个文件不在曲库里：%1").arg(firstFile));
            }
        }
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

} // namespace mamba::presentation
