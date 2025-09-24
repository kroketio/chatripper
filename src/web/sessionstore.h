#pragma once
#include <QString>
#include <QHash>
#include <QMutex>
#include <QDateTime>

class WebSessionStore {
public:
  WebSessionStore();
  ~WebSessionStore() = default;

  // create a session token for a user (returns token)
  QString createSession(const QString &username, int ttlSeconds = 3600);

  // validate token: returns true if valid and refreshes TTL
  bool validateToken(const QString &token);

  // get username for token (empty if invalid)
  QString usernameForToken(const QString &token);

  // destroy a session
  void destroySession(const QString &token);

private:
  struct SessionEntry {
    QString username;
    QDateTime expires;
  };

  QHash<QString, SessionEntry> m_sessions;
  QMutex m_mutex;
};
