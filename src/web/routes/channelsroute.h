#pragma once
#include <QHttpServer>

class SessionStore;

namespace ChannelsRoute {
  void install(QHttpServer *server, SessionStore *sessions);
}
