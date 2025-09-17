#pragma once
#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>

#include <optional>
#include "core/account.h"
#include "lib/utils.h"

class SQL {
public:
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

  static QSqlDatabase& getInstance();
  static bool initialize();
  static QSqlQuery exec(const QString &sql);
  static QSqlQuery& exec(QSqlQuery &q);
  static void create_schema();
  static bool preload_from_file(const QString &path);

  // account
  static QSharedPointer<Account> account_get_or_create(const QByteArray &username, const QByteArray &password);
  static bool account_exists(const QByteArray &username);
  static QList<QSharedPointer<Account>> account_get_all();
  static QList<QSharedPointer<Channel>> account_get_channels(const QByteArray &account_id);

  // channel
  static QSharedPointer<Channel> channel_get_or_create(const QByteArray &name, const QByteArray &topic, const QByteArray &account_owner_id);
  static bool channel_exists(const QByteArray &name);
  static QList<QSharedPointer<Channel>> channel_get_all();
  static bool channel_add_member(const QByteArray &account_id, const QByteArray &channel_id);
  static bool channel_remove_member(const QByteArray &account_id, const QByteArray &channel_id);
  static QList<QSharedPointer<Account>> channel_get_members(const QByteArray &channel_id);

  static bool insertChannel(const QString& name);
  static LoginResult insertAccount(const QString& username, const QString& password, const QString& ip, QByteArray &rtnAccountID);

  static QList<QVariantMap> getAccounts();
  static QList<QVariantMap> getChannels();
  static QList<QVariantMap> getLogins();

  static QByteArray generateUuid();
  static QString uuidToString(const QByteArray &uuidBytes);

  static QString hashPasswordBcrypt(const QString &password);
  static bool validatePasswordBcrypt(const QString &password, const QString &hash);

private:
  static QSqlDatabase db;
};
