#include "sonora/presentation/RowsModel.h"

namespace sonora::presentation
{

    RowsModel::RowsModel(QObject *parent) : QAbstractListModel{parent} {}

    int RowsModel::rowCount(const QModelIndex &parent) const
    {
        // 平表：只有根节点有子行。
        return parent.isValid() ? 0 : count_;
    }

    void RowsModel::setStatus(const QString &status)
    {
        if (status_ == status)
        {
            return;
        }
        status_ = status;
        emit statusChanged();
    }

    void RowsModel::assignCount(int count)
    {
        if (count_ == count)
        {
            return;
        }
        count_ = count;
        emit countChanged();
    }

} // namespace sonora::presentation
