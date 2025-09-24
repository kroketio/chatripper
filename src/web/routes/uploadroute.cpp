#include "web/routes/uploadroute.h"
#include "web/sessionstore.h"
#include "web/ratelimiter.h"

#include <QHttpServerResponse>
#include <QHttpHeaders>
#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QRegularExpression>
#include <QDateTime>
#include <QUuid>

#include "lib/globals.h"
#include "web/routes/utils.h"
#include "ctx.h"

// https://codeberg.org/emersion/soju/src/branch/master/doc/ext/filehost.md
// warning: you should reverse proxy this with a webserver (e.g. nginx), and enforce a max
// upload size there. This will load into memory whatever the user sends.

constexpr qint64 MAX_UPLOAD_SIZE = 5 * 1024 * 1024;

static const QStringList kAcceptedTypes = {
  "image/jpeg",
  "image/png",
  "image/gif",
  "video/mp4",
  "video/webm",
  "text/plain"
};

namespace UploadRoute {

// sanitize filenames
static QString safeFileName(const QString &filename) {
  QString base = QFileInfo(filename).fileName(); // strip directories
  const QRegularExpression validName("^[A-Za-z0-9._-]+$");
  if (!validName.match(base).hasMatch()) {
    // fallback to UUID
    base = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  return base;
}

void install(QHttpServer *server, RateLimiter *limiter) {
  // OPTIONS /api/1/file/upload
  server->route("/api/1/file/upload", QHttpServerRequest::Method::Options, [](const QHttpServerRequest &) {
    QHttpHeaders headers;
    headers.append("Allow", "OPTIONS, POST");
    headers.append("Accept-Post", kAcceptedTypes.join(", "));
    QHttpServerResponse res(QHttpServerResponder::StatusCode::NoContent);
    res.setHeaders(std::move(headers));
    return res;
  });

  // POST /api/1/file/upload
  server->route("/api/1/file/upload", QHttpServerRequest::Method::Post, [limiter](const QHttpServerRequest &request) {
    const QByteArray body = request.body();
    const QHostAddress ip = ipFromRequest(request);

    // copy cookies
    QStringList cookieHeaders;
    for (const auto &c : request.headers().values("Cookie"))
      cookieHeaders << QString::fromUtf8(c);

    // copy headers
    const QString contentType = QString::fromUtf8(request.headers().value("Content-Type"));
    const QString contentDisposition = QString::fromUtf8(request.headers().value("Content-Disposition"));

    QFuture<QHttpServerResponse> future = QtConcurrent::run([body, limiter, ip, cookieHeaders, contentType, contentDisposition, &request]() {
      // rate limit by IP
      if (auto [allowed, retryAfter] = limiter->check(ip); !allowed) {
        const qint64 seconds = QDateTime::currentDateTimeUtc().secsTo(retryAfter);
        const QString msg = QString("Too many requests, retry after %1 seconds").arg(QString::number(seconds));
        return QHttpServerResponse(msg, QHttpServerResponder::StatusCode::TooManyRequests);
      }

      const auto current_user = g::webSessions->get_user(request);
      if (current_user.isNull())
        return QHttpServerResponse("Unauthorized", QHttpServerResponder::StatusCode::Unauthorized);

      if (body.size() > MAX_UPLOAD_SIZE)
        return QHttpServerResponse("File too large", QHttpServerResponder::StatusCode::PayloadTooLarge);

      if (contentType.startsWith("multipart/form-data"))
        return QHttpServerResponse("Unsupported upload type multipart/form-data", QHttpServerResponder::StatusCode::UnsupportedMediaType);

      if (!kAcceptedTypes.contains(contentType))
        return QHttpServerResponse("Unsupported media type", QHttpServerResponder::StatusCode::UnsupportedMediaType);

      if (body.isEmpty())
        return QHttpServerResponse("empty body", QHttpServerResponder::StatusCode::BadRequest);

      // extract filename
      QString filename;
      if (!contentDisposition.isEmpty() && contentDisposition.contains("filename=")) {
        filename = contentDisposition.section("filename=", 1).trimmed();
        filename.remove('"');
        filename = safeFileName(filename);
      } else {
        filename = safeFileName(QUuid::createUuid().toString(QUuid::WithoutBraces));
      }

      const QString filePath = g::uploadsDirectory + "/" + filename;

      QFile file(filePath);
      if (!file.open(QIODevice::WriteOnly))
        return QHttpServerResponse("failed to write file", QHttpServerResponder::StatusCode::InternalServerError);

      file.write(body);
      file.flush();
      file.close();

      // build response
      const QString fileUrl = QString("/files/%1").arg(filename);
      QJsonObject bodyJson;
      bodyJson.insert("url", fileUrl);
      const QByteArray json = QJsonDocument(bodyJson).toJson();

      QHttpHeaders headers;
      headers.append("Location", fileUrl);
      QHttpServerResponse res("application/json", json, QHttpServerResponder::StatusCode::Created);
      res.setHeaders(std::move(headers));
      return res;
    });

    return future;
  });

  // GET /files/<arg> - serve uploaded file
  server->route("/files/<arg>", QHttpServerRequest::Method::Get, [](QString arg) {
    QFuture<QHttpServerResponse> future = QtConcurrent::run([arg]() {
      const QString safeFile = QFileInfo(arg).fileName();
      const QRegularExpression validName("^[A-Za-z0-9._-]+$");
      if (!validName.match(safeFile).hasMatch()) {
        return QHttpServerResponse("Invalid filename", QHttpServerResponder::StatusCode::BadRequest);
      }

      const QString filePath = g::uploadsDirectory + "/" + safeFile;
      if (!QFile::exists(filePath))
        return QHttpServerResponse("Not Found", QHttpServerResponder::StatusCode::NotFound);

      return QHttpServerResponse::fromFile(filePath);
    });

    return future;
  });

  // HEAD /files/<arg> - describe uploaded file
  server->route("/files/<arg>", QHttpServerRequest::Method::Head, [](QString arg) {
    QFuture<QHttpServerResponse> future = QtConcurrent::run([arg]() {
      const QString safeFile = QFileInfo(arg).fileName();
      const QString filePath = g::uploadsDirectory + "/" + safeFile;

      if (!QFile::exists(filePath))
        return QHttpServerResponse("Not Found", QHttpServerResponder::StatusCode::NotFound);

      const QFileInfo info(filePath);
      QHttpHeaders headers;
      headers.append("Content-Length", QString::number(info.size()));
      QHttpServerResponse res(QHttpServerResponder::StatusCode::Ok);
      res.setHeaders(std::move(headers));
      return res;
    });

    return future;
  });
}

}
