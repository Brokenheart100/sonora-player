#include "mamba/presentation/LyricsModel.h"

#include <algorithm>

#include "mamba/lyrics/contracts/Queries.h"

namespace mamba::presentation
{
    namespace
    {

        QString fromUtf8(const std::string &text) { return QString::fromStdString(text); }

        // 与 Lyrics.Core 的 LyricTimeline::indexAt 同一套规则（Presentation 看不到
        // 那个类，见头文件里的说明）：返回最后一个 timeMs <= nowMs 的下标，
        // 早于第一行返回 -1。upper_bound 找"第一个 > nowMs"，减一就是答案，
        // 于是同一时间戳的多行自然落到**后**一行。
        int indexAt(const std::vector<qint64> &times, qint64 nowMs)
        {
            const auto it = std::upper_bound(times.begin(), times.end(), nowMs);
            return static_cast<int>(it - times.begin()) - 1;
        }

    } // namespace

    LyricsModel::LyricsModel(kernel::IQueryBus &queries, QObject *parent)
        : QAbstractListModel(parent), queries_(queries) {}

    void LyricsModel::loadFor(const QString &filePath)
    {
        if (filePath == loadedPath_)
        {
            return;
        }
        loadedPath_ = filePath;

        if (filePath.isEmpty())
        {
            clear();
            return;
        }

        lyrics::contracts::GetLyrics query;
        query.filePath = filePath.toStdString();

        const auto result = queries_.ask<lyrics::contracts::LyricsView>(query);
        if (!result)
        {
            clear();
            statusText_ = fromUtf8(result.error().message());
            emit linesChanged();
            return;
        }
        adopt(result.value());
    }

    void LyricsModel::clear()
    {
        beginResetModel();
        lines_.clear();
        times_.clear();
        visible_ = false;
        statusText_.clear();
        const auto hadIndex = currentIndex_ != -1;
        currentIndex_ = -1;
        endResetModel();

        emit linesChanged();
        if (hadIndex)
        {
            emit currentIndexChanged();
        }
    }

    void LyricsModel::adopt(const lyrics::contracts::LyricsView &view)
    {
        beginResetModel();
        lines_ = view.lines;
        times_.clear();
        times_.reserve(lines_.size());
        for (const auto &line : lines_)
        {
            times_.push_back(line.timeMs);
        }
        // 纯音乐也有"一行"（Handler 塞的占位），所以可见性看的是有没有行，不是状态。
        // 一行都没有时界面不画盒子，与计划里"无歌词不显示空盒子"一致。
        visible_ = !lines_.empty();
        switch (view.status)
        {
        case lyrics::contracts::LyricsStatus::Found:
            statusText_ = fromUtf8(view.source);
            break;
        case lyrics::contracts::LyricsStatus::Instrumental:
            statusText_ = QStringLiteral("纯音乐");
            break;
        case lyrics::contracts::LyricsStatus::NotFound:
        default:
            statusText_.clear();
            break;
        }
        const auto hadIndex = currentIndex_ != -1;
        currentIndex_ = -1;
        endResetModel();

        emit linesChanged();
        if (hadIndex)
        {
            emit currentIndexChanged();
        }
    }

    void LyricsModel::setPositionMs(qint64 positionMs)
    {
        if (times_.empty())
        {
            return;
        }
        int row = indexAt(times_, positionMs);
        // LRC 里常有"有时间戳、没文字"的空行（前奏、间奏占位，比如 `[00:35.29]`）。
        // 停在这种行上，屏幕上就是"一行红的都没有"，看起来像歌词没跟上伴奏。
        // 往回退到最近一行有字的：间奏期间保持上一句高亮，是字幕的通行做法。
        while (row > 0)
        {
            const auto &line = lines_[static_cast<std::size_t>(row)];
            if (!line.text.empty() || !line.translation.empty())
            {
                break;
            }
            --row;
        }
        setCurrentIndex(row);
    }

    void LyricsModel::setCurrentIndex(int row)
    {
        if (row == currentIndex_)
        {
            return;
        }
        const auto previous = currentIndex_;
        currentIndex_ = row;
        // 只让变动的两行刷新，不整表 reset —— 30Hz 的 reset 会把滚动位置也重置掉。
        if (previous >= 0 && previous < count())
        {
            const auto changed = this->index(previous);
            emit dataChanged(changed, changed, {ActiveRole});
        }
        if (currentIndex_ >= 0 && currentIndex_ < count())
        {
            const auto changed = this->index(currentIndex_);
            emit dataChanged(changed, changed, {ActiveRole});
        }
        emit currentIndexChanged();
    }

    int LyricsModel::rowCount(const QModelIndex &parent) const
    {
        return parent.isValid() ? 0 : count();
    }

    QVariant LyricsModel::data(const QModelIndex &index, int role) const
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= count())
        {
            return {};
        }
        const auto &line = lines_[static_cast<std::size_t>(index.row())];
        switch (role)
        {
        case TextRole:
            return fromUtf8(line.text);
        case TranslationRole:
            return fromUtf8(line.translation);
        case ActiveRole:
            return index.row() == currentIndex_;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> LyricsModel::roleNames() const
    {
        return {
            {TextRole, QByteArrayLiteral("text")},
            {TranslationRole, QByteArrayLiteral("translation")},
            {ActiveRole, QByteArrayLiteral("active")},
        };
    }

} // namespace mamba::presentation
