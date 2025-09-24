#pragma once
#include <QHttpServer>

class WebSessionStore;

namespace ChannelsRoute {
  void install(QHttpServer *server);
}
