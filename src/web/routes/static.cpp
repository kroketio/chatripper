#include "web/routes/static.h"
#include "web/ratelimiter.h"

#include <QHttpServerResponse>
#include <QHttpHeaders>
#include <QtConcurrent>
#include <QFileInfo>
#include <QFile>
#include <QMimeDatabase>
#include <QDateTime>

#include "lib/globals.h"
#include "web/routes/utils.h"
#include "ctx.h"

namespace StaticRoute {

  void install(QHttpServer *server, RateLimiter *limiter) {
    server->route("/static/<arg>", [limiter] (const QUrl &url, const QHttpServerRequest &request) {
      // return QHttpServerResponse::fromFile(QStringLiteral(":/assets/%1").arg(url.path()));
      const QHostAddress ip = ipFromRequest(request);
      auto path = url.path();

      QFuture<QHttpServerResponse> future = QtConcurrent::run([path, ip, limiter]() {
        // rate limit
        if (auto [allowed, retryAfter, msg] = limiter->check(ip, "Too many requests, retry after %1 seconds"); !allowed)
          return QHttpServerResponse(msg, QHttpServerResponder::StatusCode::TooManyRequests);

        // sanitize path
        QString safePath;
        if (!sanitizePath(path, safePath))
            return QHttpServerResponse("Invalid path", QHttpServerResponder::StatusCode::BadRequest);

        const QString filePath = g::staticDirectory + "/" + safePath;
        qDebug() << filePath;
        if (!QFile::exists(filePath)) {
          return QHttpServerResponse("Not Found", QHttpServerResponder::StatusCode::NotFound);
        }

        return QHttpServerResponse::fromFile(filePath);
      });

      return future;
    });
  }

}
