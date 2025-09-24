#pragma once
#include <QHttpServer>
#include <QHttpServerRequest>

class RateLimiter;
class WebSessionStore;

namespace AuthRoute {
  void install(QHttpServer *server, RateLimiter *limiter);
}
