#pragma once
#include <QHostAddress>
#include <QDateTime>
#include <QMutex>
#include <QHash>

struct RateLimitResult {
  bool allowed;
  QDateTime retryAfter;
};

class RateLimiter {
public:
  RateLimiter(int max_requests = 5, int window_seconds = 60);
  RateLimitResult check(const QHostAddress &addr);
private:
  struct Entry {
    int count = 0;
    QDateTime windowStart;
  };

  QHash<QString, Entry> m_table;
  int m_maxRequests;
  int m_windowSeconds;
  QMutex m_mutex;
};
