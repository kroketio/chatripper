#pragma once

#include <QObject>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QTimer>
#include <QDebug>
#include <QQueue>
#include <QReadWriteLock>
#include <QThread>
#include <QMutex>
#include <QDir>

#include "lib/globals.h"
#include "lib/sql.h"

#include "web/webserver.h"

#include "core/channel.h"
#include "core/account.h"

#include "irc/client_connection.h"
#include "irc/server.h"
#include "python/manager.h"

#include <QPair>
#include <algorithm>

class Ctx final : public QObject {
Q_OBJECT

public:
  explicit Ctx();
  ~Ctx() override;
  QString configRoot;
  QString homeDir;

  irc::Server *irc_server = nullptr;
  WebServer *web_server = nullptr;
  SnakePit* snakepit = nullptr;

  mutable QReadWriteLock mtx_cache;

  QSet<QSharedPointer<Account>> accounts;
  QHash<QByteArray, QSharedPointer<Account>> accounts_lookup_name;
  QHash<QByteArray, QSharedPointer<Account>> accounts_lookup_uuid;
  void account_insert_cache(const QSharedPointer<Account>& ptr);
  void account_remove_cache(const QSharedPointer<Account>& ptr);
  bool account_username_exists(const QByteArray& username) const;
  void irc_nicks_remove_cache(const QByteArray& nick) const;
  void irc_nicks_insert_cache(const QByteArray &nick, const QSharedPointer<Account>& ptr) const;
  QSharedPointer<Account> irc_nick_get(const QByteArray &nick) const;

  // need to keep track of nicks too, as on IRC they are unique
  // they need to be lowercase
  QHash<QByteArray, QSharedPointer<Account>> irc_nicks;
  QHash<QByteArray, QSharedPointer<Channel>> channels;
  QList<QSharedPointer<Channel>> get_channels_ordered() const;
  QList<QSharedPointer<Account>> get_accounts_ordered() const;

  void startIRC(int port, const QByteArray& password);

  static Ctx* instance() {
    return g::ctx;
  }

  QList<QVariantMap> getAccountsByUUIDs(const QList<QByteArray> &uuids) const;
  QList<QVariantMap> getChannelsByUUIDs(const QList<QByteArray> &uuids) const;

signals:
  void applicationLog(const QString &msg);

private slots:
  // void onChannelMemberJoined(const QSharedPointer<Account> &account);
  // void onChannelMemberJoinedFailed(const QSharedPointer<Account> &account);
  void onApplicationLog(const QString &msg);

private:
  QThread* m_web_thread = nullptr;
  QFileInfo m_path_db;

  static void createConfigDirectory(const QStringList &lst);
  static void createDefaultFiles();
};
