#include <QJsonDocument>
#include <QJsonObject>
#include <QHttpServerResponse>
#include <QHttpHeaders>
#include <QtConcurrent>
#include <QHostAddress>
#include <QJsonParseError>

#include "web/routes/authroute.h"
#include "web/ratelimiter.h"
#include "web/sessionstore.h"
#include "web/routes/utils.h"

#include "core/qtypes.h"
#include "lib/bcrypt/bcrypt.h"
#include "ctx.h"

namespace AuthRoute {

QHttpServerResponse create_session(const QString& username, SessionStore *sessions) {
  // create session
  const QString token = sessions->createSession(username);

  // prepare response JSON
  const QByteArray json = QJsonDocument(QJsonObject{{"ok", true}}).toJson();

  // build headers
  QHttpHeaders headers;
  headers.insert(0, "Set-Cookie", QString("session=%1; Path=/; HttpOnly; SameSite=Lax").arg(token));

  // build response with MIME, body and status, then attach headers
  QHttpServerResponse res("application/json", json, QHttpServerResponder::StatusCode::Created);
  res.setHeaders(std::move(headers));
  return res;
}

void install(QHttpServer *server, RateLimiter *limiter, SessionStore *sessions) {
  // POST /api/1/login
  server->route("/api/1/login", QHttpServerRequest::Method::Post, [limiter, sessions](const QHttpServerRequest &request) {
    const QHostAddress ip = ipFromRequest(request);

    QFuture<QHttpServerResponse> future = QtConcurrent::run([&request, ip, limiter, sessions]() {
      // rate limit by IP
      if (auto [allowed, retryAfter] = limiter->check(ip); !allowed) {
        const qint64 seconds = QDateTime::currentDateTimeUtc().secsTo(retryAfter);
        const QString msg = QString("Too many logins, retry after %1 seconds").arg(QString::number(seconds));
        return QHttpServerResponse(msg, QHttpServerResponder::StatusCode::TooManyRequests);
      }

      // expect JSON body
      const QByteArray body = request.body();
      QJsonParseError err;
      const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
      if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QHttpServerResponse("invalid json", QHttpServerResponder::StatusCode::BadRequest);

      const QJsonObject obj = doc.object();
      const QString username = obj.value("username").toString();
      const QString password = obj.value("password").toString();

      if (g::ctx->snakepit->hasEventHandler(QIRCEvent::AUTH_SASL_PLAIN)) {
        const auto res = g::ctx->snakepit->event(
          QIRCEvent::AUTH_SASL_PLAIN,
          username,
          password,
          ip.toString());

        if (!res.canConvert<QAuthUserResult>())
          return create_session(username, sessions);

        return QHttpServerResponse("invalid credentials", QHttpServerResponder::StatusCode::Unauthorized);
      }

      const auto account = Account::get_by_name(username.toUtf8());
      if (account.isNull())
        return QHttpServerResponse("invalid credentials", QHttpServerResponder::StatusCode::Unauthorized);

      const std::string candidateStr = password.toStdString();
      const std::string pw = account->password().toStdString();
      const bool result = bcrypt::validatePassword(candidateStr, pw);

      if (!result)
        return QHttpServerResponse("invalid credentials", QHttpServerResponder::StatusCode::Unauthorized);

      return create_session(username, sessions);
    });

    return future;
  });
}

}
