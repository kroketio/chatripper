#pragma once
#include <QHttpServer>

class RateLimiter;

namespace StaticRoute {
  void install(QHttpServer *server, RateLimiter *limiter);
}
