#include "sonora/platform/net/QtHttpTransport.h"

#include <chrono>
#include <utility>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>

namespace sonora::platform::net
{
    namespace
    {

        namespace c = online::contracts;

        kernel::ErrorCode mapNetworkError(QNetworkReply::NetworkError error, QString *detail)
        {
            switch (error)
            {
            case QNetworkReply::OperationCanceledError:
                return kernel::ErrorCode::Internal; // "已取消"由调用方补 message
            case QNetworkReply::TimeoutError:
                return kernel::ErrorCode::Timeout;
            case QNetworkReply::SslHandshakeFailedError:
                return kernel::ErrorCode::Upstream;
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::TemporaryNetworkFailureError:
                return kernel::ErrorCode::Unavailable;
            case QNetworkReply::ContentAccessDenied:
            case QNetworkReply::ContentNotFoundError:
            case QNetworkReply::ContentOperationNotPermittedError:
            case QNetworkReply::UnknownContentError:
            case QNetworkReply::ProtocolInvalidOperationError:
            case QNetworkReply::ProtocolFailure:
            case QNetworkReply::ProtocolUnknownError:
                // 有 HTTP 状态码的走正常响应，这里只是兜底（detail 由调用方补）。
                return kernel::ErrorCode::Upstream;
            default:
                if (detail != nullptr && detail->isEmpty())
                {
                    *detail = QStringLiteral("网络错误");
                }
                return kernel::ErrorCode::Unavailable;
            }
        }

        bool isHttpsUrl(const std::string &url)
        {
            const QUrl parsed(QString::fromStdString(url));
            return parsed.isValid() && parsed.scheme() == QLatin1String("https");
        }

    } // namespace

    struct QtHttpTransport::Impl
    {
        QNetworkAccessManager manager;
        QHash<std::uint64_t, QNetworkReply *> inflight;
    };

    QtHttpTransport::QtHttpTransport() : impl_(std::make_unique<Impl>())
    {
        // QNAM 的 thread affinity 决定了它只能在自己所在线程上用。
        const auto *app = QCoreApplication::instance();
        Q_ASSERT(app != nullptr);
        Q_ASSERT(QThread::currentThread() != app->thread());
    }

    QtHttpTransport::~QtHttpTransport()
    {
        for (QNetworkReply *reply : std::as_const(impl_->inflight))
        {
            reply->disconnect();
            reply->abort();
            reply->deleteLater();
        }
        impl_->inflight.clear();
    }

    void QtHttpTransport::send(std::uint64_t requestId, const c::HttpRequest &request, c::HttpCallback callback)
    {
        if (!callback)
        {
            return;
        }

        if (!isHttpsUrl(request.url))
        {
            // 明文或相对地址一律拒绝：契约里写的是"API 一律 https"。
            std::move(callback)(kernel::Result<c::HttpResponse>::fail(
                kernel::Error{kernel::ErrorCode::InvalidArgument, "在线接口只允许 https://"}));
            return;
        }

        QNetworkRequest netRequest{QUrl(QString::fromStdString(request.url))};
        netRequest.setTransferTimeout(request.transferTimeout);
        netRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                               QNetworkRequest::NoLessSafeRedirectPolicy);
        for (const auto &header : request.headers)
        {
            netRequest.setRawHeader(QByteArray::fromStdString(header.first),
                                    QByteArray::fromStdString(header.second));
        }

        QNetworkReply *reply = nullptr;
        const QByteArray body = QByteArray::fromStdString(request.body);
        switch (request.method)
        {
        case c::HttpMethod::Get:
            reply = impl_->manager.get(netRequest);
            break;
        case c::HttpMethod::Post:
            reply = impl_->manager.post(netRequest, body);
            break;
        }

        if (reply == nullptr)
        {
            std::move(callback)(kernel::Result<c::HttpResponse>::fail(
                kernel::Error{kernel::ErrorCode::Internal, "无法创建网络请求"}));
            return;
        }

        impl_->inflight.insert(requestId, reply);
        const std::int64_t maxBodyBytes = request.maxBodyBytes;
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();

        QObject::connect(reply, &QNetworkReply::finished, reply,
                         [this, requestId, reply, callback = std::move(callback), timer, maxBodyBytes]() mutable {
                             impl_->inflight.remove(requestId);
                             reply->deleteLater();

                             const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                             const QByteArray payload = reply->readAll().left(
                                 static_cast<qsizetype>(maxBodyBytes > 0 ? maxBodyBytes : 0));

                             // 有状态码就是"上游给了答复"，错误码也当正常响应用——
                             // 状态码 → 语义的翻译只有 ErrorMap 一处，传输层不做业务判断。
                             if (status > 0)
                             {
                                 c::HttpResponse response;
                                 response.statusCode = status;
                                 response.body = payload.toStdString();
                                 response.contentType =
                                     reply->rawHeader("Content-Type").toStdString();
                                 response.elapsed =
                                     std::chrono::milliseconds{timer->elapsed()};
                                 std::move(callback)(kernel::Result<c::HttpResponse>::ok(std::move(response)));
                                 return;
                             }

                             QString detail;
                             const kernel::ErrorCode code = mapNetworkError(reply->error(), &detail);
                             if (reply->error() == QNetworkReply::OperationCanceledError)
                             {
                                 detail = QStringLiteral("已取消");
                             }
                             else if (detail.isEmpty())
                             {
                                 detail = reply->errorString();
                             }
                             std::move(callback)(kernel::Result<c::HttpResponse>::fail(
                                 kernel::Error{code, detail.toStdString()}));
                         });
    }

    void QtHttpTransport::cancel(std::uint64_t requestId)
    {
        QNetworkReply *reply = impl_->inflight.value(requestId, nullptr);
        if (reply != nullptr)
        {
            reply->abort();
        }
    }

} // namespace sonora::platform::net
