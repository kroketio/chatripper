#include "web/routes/channelsroute.h"
#include "web/sessionstore.h"

#include <QHttpServerResponse>
#include <QtConcurrent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "web/routes/utils.h"

#include "ctx.h"
#include "lib/utils.h"
#include "lib/logger_std/logger_std.h"
#include "core/channel.h"

namespace ChannelsRoute {

void install(QHttpServer *server, SessionStore *sessions) {
  server->route("/api/1/channels", QHttpServerRequest::Method::Get, [sessions](const QHttpServerRequest &request) {
    QFuture<QHttpServerResponse> future = QtConcurrent::run([&request, sessions] {
      const QString token = tokenFromRequest(request);
      if (token.isEmpty() || !sessions->validateToken(token))
        return QHttpServerResponse("Unauthorized", QHttpServerResponder::StatusCode::Unauthorized);

      rapidjson::Document root;
      root.SetObject();
      auto& allocator = root.GetAllocator();

      rapidjson::Value channelsArray(rapidjson::kArrayType);
      for (const auto &c : g::ctx->channels.values()) {
          if (!c)
            continue;
        channelsArray.PushBack(c->to_rapidjson(allocator), allocator);
      }

      root.AddMember("channels", channelsArray, allocator);

      // serialize
      rapidjson::StringBuffer buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      root.Accept(writer);

      QByteArray jsonData(buffer.GetString(), static_cast<int>(buffer.GetSize()));
      return QHttpServerResponse("application/json", jsonData, QHttpServerResponder::StatusCode::Ok);
    });
    return future;
  });
}

}
