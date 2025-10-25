#pragma once
#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QSharedPointer>
#include <QHash>
#include <QUuid>
#include <QReadWriteLock>
#include <QVariantMap>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "lib/globals.h"
#include "core/qtypes.h"

class Account;
class Channel;
class Role;

class Server final : public QObject {
Q_OBJECT

public:
  explicit Server(const QByteArray& server_name = "", QObject* parent = nullptr);

  static QSharedPointer<Server> create_from_db(
      const QUuid &id,
      const QByteArray &name,
      const QSharedPointer<Account> &owner,
      const QDateTime &creation);

  static QSharedPointer<Server> create();

  static QSharedPointer<Server> get_by_uid(const QUuid &uid);
  static QSharedPointer<Server> get_by_name(const QByteArray &name);

  QList<QSharedPointer<Account>> all_accounts() const;
  void add_account(const QSharedPointer<Account> &acc);
  void remove_account(const QUuid &account_uid);

  void merge(const QSharedPointer<Server> &from);

  QByteArray name() const;
  void setName(const QByteArray &name);

  QUuid uid() const;
  void setUID(const QUuid &uid);
  QByteArray uid_str() const { return m_uid_str; }

  [[nodiscard]] QSharedPointer<Account> accountOwner() const;
  void setAccountOwner(const QSharedPointer<Account> &owner);

  void add_channel(const QSharedPointer<Channel> &channel);
  void remove_channel(const QSharedPointer<Channel> &channel);
  QList<QSharedPointer<Channel>> all_channels() const;

  void add_role(const QSharedPointer<Role> &role);
  void remove_role(const QSharedPointer<Role> &role);
  QSharedPointer<Role> role_by_name(const QByteArray &name) const;
  QList<QSharedPointer<Role>> all_roles() const;

  [[nodiscard]] bool is_owned_by(const QUuid &account_uid) const;

  ~Server() override;

  QDateTime creation_date;

signals:
  void nameChanged(const QByteArray& old_name, const QByteArray& new_name);

private:
  mutable QReadWriteLock mtx_lock;

  QUuid m_uid;
  QByteArray m_uid_str;
  QByteArray m_name;
  QSharedPointer<Account> m_owner;

  QHash<QUuid, QSharedPointer<Channel>> m_channels;
  QHash<QUuid, QSharedPointer<Role>> m_roles;
  QHash<QUuid, QSharedPointer<Account>> m_accounts;

public:
  QVariantMap to_variantmap() const;
  rapidjson::Value to_rapidjson(rapidjson::Document::AllocatorType& allocator, bool include_channels = false, bool include_roles = false, bool include_accounts = false) const;
};
