#include "utils.h"

QHostAddress ipFromRequest(const QHttpServerRequest &req) {
  // prefer X-Forwarded-For when behind a proxy
  // @TODO: only adopt x-forwarded-for when actually behind a proxy; introduce a user setting for this
  const auto forwarded = req.headers().values("X-Forwarded-For");
  if (!forwarded.isEmpty()) {
    const QStringList parts = QString::fromUtf8(forwarded.first()).split(',', Qt::SkipEmptyParts);
    return QHostAddress(parts.first().trimmed());
  }

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  // available in Qt 6.6+
  return req.remoteAddress();
#else
  return QHostAddress("127.0.0.1");
#endif
}

QString tokenFromRequest(const QHttpServerRequest &req) {
  const auto cookies = req.headers().values("Cookie");
  if (cookies.isEmpty())
    return {};

  const QList<QByteArray> parts = cookies.first().split(';');
  for (const auto &p : parts) {
    const auto pair = QString::fromUtf8(p).split('=', Qt::SkipEmptyParts);
    if (pair.size() == 2 && pair.at(0).trimmed() == "session")
      return pair.at(1).trimmed();
  }
  return {};
}