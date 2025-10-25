#pragma once
#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>

#include <optional>
#include "core/account.h"
#include "core/permission.h"
#include "core/server.h"
#include "core/upload.h"
#include "lib/utils.h"

namespace sql {
  extern std::unordered_map<unsigned long, QSqlDatabase*> DB_INSTANCES;

  struct MetadataResult {
    QMap<QString, QVariant> keyValues;
    QMap<QString, QSet<QSharedPointer<Account>>> subscribers;
  };

  enum class RefType {
    Channel,
    Account
};

  enum class LoginResult {
    Success,
    AccountNotFound,
    InvalidPassword,
    DatabaseError
  };

  enum class EventType {
    Message = 0,
    ChannelJoin = 1,
    ChannelLeave = 2
  };

  // static QSqlDatabase& getInstance();
  bool initialize();
  QSharedPointer<QSqlQuery> exec(const QString &sql);
  QSharedPointer<QSqlQuery> exec(const QSharedPointer<QSqlQuery> &q);
  void create_schema();
  bool preload_from_file(const QString &path);

  // metadata
  // @TODO: deal with the removal of the resource backing ref_id
  MetadataResult metadata_get(QUuid ref_id);
  bool metadata_modify(QUuid ref_id, const QByteArray& key, const QByteArray& new_value);
  bool metadata_remove(const QByteArray& key, QUuid ref_id);
  QUuid metadata_create(const QByteArray& key, const QByteArray& value, const QUuid ref_id, RefType ref_type);
  bool metadata_upsert(const QByteArray& key, const QByteArray& value, QUuid ref_id, RefType ref_type);
  bool metadata_unsubscribe(QUuid ref_id, const QByteArray& key, QUuid account_id);
  bool metadata_subscribe(QUuid ref_id, const QByteArray& key, QUuid account_id);
  bool metadata_subscribe_bulk(QUuid ref_id, const QList<QByteArray>& keys, QUuid account_id);
  bool metadata_unsubscribe_bulk(QUuid ref_id, const QList<QByteArray>& keys, QUuid account_id);

  // account
  QSharedPointer<Account> account_get_or_create(const QByteArray &username, const QByteArray &password);
  QSharedPointer<Upload> upload_get_or_create(const QUuid &accountId, const QString &path, int type, int variant);
  QSharedPointer<Permission> permission_get_or_create(const QUuid &roleId, Permission::PermissionFlags flags);

  bool account_exists(const QByteArray &username);
  QList<QSharedPointer<Account>> account_get_all();
  QList<QSharedPointer<Channel>> account_get_channels(const QUuid &account_id);

  // channel
  QSharedPointer<Channel> channel_get_or_create(const QByteArray &name, const QByteArray &topic, const QSharedPointer<Account> &owner, const QSharedPointer<Server> &server);
  bool channel_exists(const QByteArray &name);
  QList<QSharedPointer<Channel>> channel_get_all();
  bool channel_add_member(const QUuid &account_id, const QUuid &channel_id);
  bool channel_remove_member(const QUuid &account_id, const QUuid &channel_id);
  QList<QSharedPointer<Account>> channel_get_members(const QUuid &channel_id);

  // server
  bool server_add_member(const QUuid &accountId, const QUuid &serverId);
  QSharedPointer<Server> server_get_or_create(const QByteArray &name, const QSharedPointer<Account> &owner);
  QSharedPointer<Role> create_role_for_server(
      const QSharedPointer<Server> &server,
      const QString &roleName,
      int priority = 0,
      const QUuid &iconId = QUuid(),
      bool assignToExistingMembers = true,
      Permission::PermissionFlags defaultPermissions = Permission::PermissionFlags());
  bool assign_role_to_account(const QUuid &accountId, const QSharedPointer<Role> &role);

  // messages
  void insert_messages(const QSet<QSharedPointer<QEventMessage>> &messages);
  QUuid insert_message(const QSharedPointer<QEventMessage> &msg);

  bool insertChannel(const QString& name);
  LoginResult insertAccount(const QString& username, const QString& password, const QString& ip, QUuid &rtnAccountID);

  QList<QVariantMap> getAccounts();
  QList<QVariantMap> getChannels();
  QList<QVariantMap> getLogins();

  QString hashPasswordBcrypt(const QString &password);
  bool validatePasswordBcrypt(const QString &password, const QString &hash);
}