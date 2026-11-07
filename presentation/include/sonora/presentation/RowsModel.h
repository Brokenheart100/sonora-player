// 异步填行列表的共同骨架。
//
// 曲库与在线两个列表的差别只在"行从哪来"，剩下的东西一模一样：
// `count` 给侧栏计数用、`status` 给空态文案用、整批替换时的
// beginResetModel/endResetModel 配对。抽出来不是为了省那 20 行，
// 是为了让"什么时候该 reset、什么时候该 insert"只有一种写法。
#pragma once

#include <QAbstractListModel>
#include <QString>

namespace sonora::presentation
{

    class RowsModel : public QAbstractListModel
    {
        Q_OBJECT
        Q_PROPERTY(int count READ count NOTIFY countChanged)
        Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY statusChanged)

    public:
        explicit RowsModel(QObject *parent = nullptr);

        [[nodiscard]] int count() const noexcept { return count_; }
        [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;

        [[nodiscard]] QString status() const { return status_; }
        void setStatus(const QString &status);

    signals:
        void countChanged();
        void statusChanged();

    protected:
        // 子类在 endResetModel() 之后调用它：行数变了，侧栏计数跟着变。
        void assignCount(int count);

    private:
        int count_ = 0;
        QString status_;
    };

} // namespace sonora::presentation
