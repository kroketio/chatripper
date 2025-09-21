#include "web/routes/channelsroute.h"
#include "web/sessionstore.h"

#include <QHttpServerResponse>
#include <QtConcurrent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "web/routes/utils.h"

#include "ctx.h"
#include "core/channel.h"

namespace ChannelsRoute {

void install(QHttpServer *server, SessionStore *sessions) {
  server->route("/api/1/channels", QHttpServerRequest::Method::Get, [sessions](const QHttpServerRequest &request) {
    QFuture<QHttpServerResponse> future =
        QtConcurrent::run([&request, sessions]() {
      // @TODO: require session ?
      // const QString token = tokenFromRequest(request);
      // if (token.isEmpty() || !sessions->validateToken(token)) {
      //   return QHttpServerResponse("Unauthorized",
      //                              QHttpServerResponder::StatusCode::Unauthorized);
      // }

      QJsonArray arr;
      for (const auto &c : g::ctx->channels.values()) {
        if (!c)
          continue;
        QJsonObject o;
        o.insert("id", QString::fromUtf8(c->uid));
        o.insert("name", QString::fromUtf8(c->name()));
        o.insert("topic", QString::fromUtf8(c->topic()));
        arr.append(o);
      }

      QJsonObject root;
      root.insert("channels", arr);

      return QHttpServerResponse("application/json", QJsonDocument(root).toJson(), QHttpServerResponder::StatusCode::Ok);
    });
    return future;
  });
}

}
