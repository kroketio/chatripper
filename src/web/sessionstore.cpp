#include "sessionstore.h"
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QHttpServerRequest>

#include "ctx.h"
#include "core/account.h"

WebSessionStore::WebSessionStore() = default;

static QString randomToken(int length = 32) {
  QByteArray bytes;
  bytes.resize(length);
  for (int i = 0; i < length; ++i)
    bytes[i] = char(QRandomGenerator::global()->bounded(33, 127));
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QString WebSessionStore::createSession(const QString &username, int ttlSeconds) {
  QMutexLocker locker(&m_mutex);
  const QString token = randomToken(32);
  SessionEntry e;
  e.username = username;
  e.expires = QDateTime::currentDateTimeUtc().addSecs(ttlSeconds);
  m_sessions.insert(token, e);
  return token;
}

bool WebSessionStore::validateToken(const QString &token) {
  QMutexLocker locker(&m_mutex);
  auto it = m_sessions.find(token);
  if (it == m_sessions.end())
    return false;
  if (it->expires < QDateTime::currentDateTimeUtc()) {
    m_sessions.erase(it);
    return false;
  }
  // refresh TTL (sliding)
  it->expires = QDateTime::currentDateTimeUtc().addSecs(3600);
  return true;
}

QString WebSessionStore::usernameForToken(const QString &token) {
  QMutexLocker locker(&m_mutex);
  auto it = m_sessions.find(token);
  if (it == m_sessions.end())
    return {};
  return it->username;
}

QSharedPointer<Account> WebSessionStore::get_user(const QHttpServerRequest &request) {
  QReadLocker locker(&g::ctx->mtx_cache);
  const QString token = tokenFromRequest(request);
  if (token.isEmpty() || !g::webSessions->validateToken(token))
    return {};

  const auto username = usernameForToken(token);
  if (username.isEmpty())
    return {};

  return g::ctx->accounts_lookup_name.value(username.toUtf8());
}

void WebSessionStore::destroySession(const QString &token) {
  QMutexLocker locker(&m_mutex);
  m_sessions.remove(token);
}
