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

#include "web/routes/utils.h"

#include "ctx.h"

// https://github.com/progval/ircv3-specifications/blob/filehost/extensions/filehost.md

constexpr qint64 MAX_UPLOAD_SIZE = 5 * 1024 * 1024;

namespace UploadRoute {

// sanitize filenames
static QString safeFileName(const QString &filename) {
  QString base = QFileInfo(filename).fileName(); // strip directories
  QRegularExpression validName("^[A-Za-z0-9._-]+$");
  if (!validName.match(base).hasMatch()) {
    // fallback to UUID
    base = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  return base;
}

void install(QHttpServer *server, RateLimiter *limiter, SessionStore *sessions) {
  // POST /api/1/file/upload
  // warning: you should reverse proxy this with a webserver (e.g. nginx), and enforce a max
  // upload size there. This will load into memory whatever the user sends.
  server->route("/api/1/file/upload", QHttpServerRequest::Method::Post, [sessions, limiter](const QHttpServerRequest &request) {
    const QByteArray body = request.body();

    QStringList cookieHeaders;
    for (const auto &c : request.headers().values("Cookie"))
      cookieHeaders << QString::fromUtf8(c);

    const QHostAddress ip = ipFromRequest(request);

    QFuture<QHttpServerResponse> future = QtConcurrent::run([body, limiter, ip, cookieHeaders, sessions]() {
      // rate limit by IP
      if (auto [allowed, retryAfter] = limiter->check(ip); !allowed) {
        const qint64 seconds = QDateTime::currentDateTimeUtc().secsTo(retryAfter);
        const QString msg = QString("Too many requests, retry after %1 seconds").arg(QString::number(seconds));
        return QHttpServerResponse(msg, QHttpServerResponder::StatusCode::TooManyRequests);
      }

      if (body.size() > MAX_UPLOAD_SIZE)
        return QHttpServerResponse("File too large", QHttpServerResponder::StatusCode::PayloadTooLarge);

      // const QString token = tokenFromCookies(cookieHeaders);
      // if (token.isEmpty() || !sessions->validateToken(token)) {
      //   return QHttpServerResponse("Unauthorized",
      //                              QHttpServerResponder::StatusCode::Unauthorized);
      // }

      if (body.isEmpty())
        return QHttpServerResponse("empty body", QHttpServerResponder::StatusCode::BadRequest);

      // safe filename
      const QString filename = safeFileName(QString::fromUtf8(QUuid::createUuid().toString().toUtf8()));
      const QString filePath = g::uploadsDirectory + "/" + filename;

      QFile file(filePath);
      if (!file.open(QIODevice::WriteOnly)) {
        return QHttpServerResponse("failed to write file", QHttpServerResponder::StatusCode::InternalServerError);
      }

      file.write(body);
      file.flush();
      file.close();

      // build response
      const QString fileUrl = QString("/files/%1").arg(filename);
      QJsonObject bodyJson;
      bodyJson.insert("url", fileUrl);
      const QByteArray json = QJsonDocument(bodyJson).toJson();

      QHttpHeaders headers;
      headers.insert(0, "Location", fileUrl);
      QHttpServerResponse res("application/json", json, QHttpServerResponder::StatusCode::Created);
      res.setHeaders(std::move(headers));
      return res;
    });

    return future;
  });

  // GET /files/<arg> - serve uploaded files
  server->route("/files/<arg>", [](QString arg) {
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
}

}
