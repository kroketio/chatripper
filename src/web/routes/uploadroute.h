#pragma once
#include <QHttpServer>

class SessionStore;
class RateLimiter;

namespace UploadRoute {
  void install(QHttpServer *server, RateLimiter *limiter, SessionStore *sessions);
}
