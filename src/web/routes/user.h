#pragma once
#include <QHttpServer>

class WebSessionStore;

namespace UsersRoute {
  void install(QHttpServer *server);
}
