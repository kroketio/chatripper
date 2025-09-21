#pragma once
#include <QHttpServer>
#include <QHttpServerRequest>

class RateLimiter;
class SessionStore;

namespace AuthRoute {
  void install(QHttpServer *server, RateLimiter *limiter, SessionStore *sessions);
}
