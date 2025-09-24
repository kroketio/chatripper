#pragma once
#include <QHttpServer>

class WebSessionStore;
class RateLimiter;

namespace UploadRoute {
  void install(QHttpServer *server, RateLimiter *limiter);
}
