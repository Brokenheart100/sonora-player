#include "mamba/presentation/TrackListModel.h"

#include <QString>
#include <utility>

#include "mamba/catalog/contracts/Commands.h"

namespace mamba::presentation {
namespace {

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
    if (milliseconds <= 0) {
        return QStringLiteral("--:--");
    }
    const auto totalSeconds = milliseconds / 1000;
    const auto minutes = totalSeconds / 60;
    const auto seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

}  // namespace

TrackListModel::TrackListModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                               QObject *parent)
    : QAbstractListModel(parent), commands_(commands), queries_(queries)
{
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;   // 列表模型没有树形结构
    }
    return static_cast<int>(rows_.size());
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }
    const auto &row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
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
    if (status_ == text) {
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
    if (page) {
        rows_ = page.value().rows;
        setStatus(QStringLiteral("共 %1 首").arg(static_cast<qlonglong>(page.value().totalCount)));
    } else {
        rows_.clear();
        setStatus(fromUtf8(page.error().message()));
    }
    endResetModel();

    emit countChanged();
}

void TrackListModel::importFolder(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        setStatus(QStringLiteral("没有选择文件夹"));
        return;
    }

    catalog::contracts::ImportFolder command;
    command.folderPath = path.toUtf8().toStdString();

    const auto result = commands_.dispatch(command);
    if (!result) {
        setStatus(fromUtf8(result.error().message()));
        return;
    }
    // 导入写完再刷新：读侧看到的一定是已提交的数据（事务先提交，再刷新列表）。
    refresh();
}

}  // namespace mamba::presentation
