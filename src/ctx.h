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
#include "core/role.h"
#include "core/upload.h"
#include "core/server.h"
#include "core/permission.h"

#include "irc/client_connection.h"
#include "irc/threaded_server.h"
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

  irc::ThreadedServer* irc_server = nullptr;
  irc::ThreadedServer* irc_ws = nullptr;
  WebServer *web_server = nullptr;
  SnakePit* snakepit = nullptr;

  mutable QReadWriteLock mtx_cache;

  QSet<QSharedPointer<Account>> accounts;
  QHash<QByteArray, QSharedPointer<Account>> accounts_lookup_name;
  QHash<QUuid, QSharedPointer<Account>> accounts_lookup_uuid;
  void account_insert_cache(const QSharedPointer<Account>& ptr);
  void account_remove_cache(const QSharedPointer<Account>& ptr);
  bool account_username_exists(const QByteArray& username) const;
  void irc_nicks_remove_cache(const QByteArray& nick) const;
  void irc_nicks_insert_cache(const QByteArray &nick, const QSharedPointer<Account>& ptr) const;
  QSharedPointer<Account> irc_nick_get(const QByteArray &nick) const;

  // servers
  QSet<QSharedPointer<Server>> servers;
  QHash<QByteArray, QSharedPointer<Server>> servers_lookup_name;
  QHash<QUuid, QSharedPointer<Server>> servers_lookup_uuid;
  void server_insert_cache(const QSharedPointer<Server>& ptr);
  void server_remove_cache(const QSharedPointer<Server>& ptr);

  // roles
  QSet<QSharedPointer<Role>> roles;
  QHash<QByteArray, QSharedPointer<Role>> roles_lookup_name;
  QHash<QUuid, QSharedPointer<Role>> roles_lookup_uuid;
  void role_insert_cache(const QSharedPointer<Role>& ptr);
  void role_remove_cache(const QSharedPointer<Role>& ptr);

  // uploads
  QSet<QSharedPointer<Upload>> uploads;
  QHash<QUuid, QSharedPointer<Upload>> uploads_lookup_uuid;
  void upload_insert_cache(const QSharedPointer<Upload>& ptr);
  void upload_remove_cache(const QSharedPointer<Upload>& ptr);

  // permissions
  QSet<QSharedPointer<Permission>> permissions;
  QHash<QUuid, QSharedPointer<Permission>> permissions_lookup_uuid;
  void permission_insert_cache(const QSharedPointer<Permission>& ptr);
  void permission_remove_cache(const QSharedPointer<Permission>& ptr);

  // messages
  void queueMessageForInsert(const QSharedPointer<QEventMessage>& msg);

  // need to keep track of nicks too, as on IRC they are unique
  // they need to be lowercase
  QHash<QByteArray, QSharedPointer<Account>> irc_nicks;
  QHash<QByteArray, QSharedPointer<Channel>> channels;
  QList<QSharedPointer<Channel>> get_channels_ordered() const;
  QList<QSharedPointer<Account>> get_accounts_ordered() const;

  static Ctx* instance() {
    return g::ctx;
  }

  QList<QVariantMap> getAccountsByUUIDs(const QList<QUuid> &uuids) const;
  QList<QVariantMap> getChannelsByUUIDs(const QList<QUuid> &uuids) const;

signals:
  void applicationLog(const QString &msg);

private slots:
  // void onChannelMemberJoined(const QSharedPointer<Account> &account);
  // void onChannelMemberJoinedFailed(const QSharedPointer<Account> &account);
  void onApplicationLog(const QString &msg);

private:
  QThread* m_web_thread = nullptr;
  QFileInfo m_path_db;

  QReadWriteLock mtx_messageInsertion;
  QSet<QSharedPointer<QEventMessage>> m_messageInsertionQueue;
  QTimer* m_insertTimer;

  static void createConfigDirectory(const QStringList &lst);
  static void createDefaultFiles();
};
