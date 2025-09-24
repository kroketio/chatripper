#include <QHttpServerResponse>
#include <QtConcurrent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "web/routes/channelsroute.h"
#include "web/sessionstore.h"
#include "web/routes/utils.h"

#include "ctx.h"
#include "core/channel.h"
#include "lib/utils.h"
#include "lib/logger_std/logger_std.h"

namespace ChannelsRoute {

void install(QHttpServer *server) {
  server->route("/api/1/channels", QHttpServerRequest::Method::Get, [](const QHttpServerRequest &request) {
    QFuture<QHttpServerResponse> future = QtConcurrent::run([&request] {
      const auto current_user = g::webSessions->get_user(request);
      if (current_user.isNull())
        return QHttpServerResponse("Unauthorized", QHttpServerResponder::StatusCode::Unauthorized);

      rapidjson::Document root;
      root.SetObject();
      auto& allocator = root.GetAllocator();

      rapidjson::Value arr(rapidjson::kArrayType);
      for (const auto &c : g::ctx->get_channels_ordered()) {
          if (!c)
            continue;
        arr.PushBack(c->to_rapidjson(allocator), allocator);
      }

      root.AddMember("channels", arr, allocator);

      // serialize
      rapidjson::StringBuffer buffer;
      rapidjson::Writer writer(buffer);
      root.Accept(writer);

      QByteArray jsonData(buffer.GetString(), static_cast<int>(buffer.GetSize()));
      return QHttpServerResponse("application/json", jsonData, QHttpServerResponder::StatusCode::Ok);
    });
    return future;
  });
}

}
