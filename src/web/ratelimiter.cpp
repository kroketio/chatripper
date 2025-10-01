#include "ratelimiter.h"
#include <QMutexLocker>

RateLimiter::RateLimiter(const int max_requests, const int window_seconds) : m_maxRequests(max_requests), m_windowSeconds(window_seconds) {}

RateLimitResult RateLimiter::check(const QHostAddress &addr, const QString &msg) {
  const QString key = addr.toString();
  QMutexLocker locker(&m_mutex);

  const QDateTime now = QDateTime::currentDateTimeUtc();
  const auto it = m_table.find(key);

  if (it == m_table.end()) {
    Entry e;
    e.count = 1;
    e.windowStart = now;
    m_table.insert(key, e);
    return {true, {}, {}};
  }

  Entry &e = it.value();
  const qint64 elapsed = e.windowStart.secsTo(now);

  if (elapsed >= m_windowSeconds) {
    // reset window
    e.count = 1;
    e.windowStart = now;
    return {true, {}, {}};
  }

  if (e.count < m_maxRequests) {
    e.count++;
    return {true, {}, {}};
  }

  // rate limit exceeded, calculate retry time
  const QDateTime retry = e.windowStart.addSecs(m_windowSeconds);

  const qint64 seconds = QDateTime::currentDateTimeUtc().secsTo(retry);
  return {false, retry, msg.arg(QString::number(seconds))};
}
