#include "server.h"
#include "channel.h"
#include "role.h"
#include "ctx.h"
#include "lib/globals.h"
#include <QUuid>
#include <QDebug>

Server::Server(const QByteArray& server_name, QObject* parent) : m_name(server_name), QObject(parent) {
  qDebug() << "new server" << server_name;
}

QSharedPointer<Server> Server::create() {
  auto server = QSharedPointer<Server>(new Server());
  if (g::mainThread != QThread::currentThread())
    server->moveToThread(g::mainThread);
  return server;
}

QSharedPointer<Server> Server::create_from_db(
  const QUuid &id,
  const QByteArray &name,
  const QSharedPointer<Account> &owner,
  const QDateTime &creation
) {
  if (const auto ptr = get_by_uid(id); !ptr.isNull())
    return ptr;

  auto server = QSharedPointer<Server>(new Server(name));
  if (g::mainThread != QThread::currentThread())
    server->moveToThread(g::mainThread);

  server->setUID(id);
  server->setName(name);
  server->setAccountOwner(owner);
  server->creation_date = creation;

  g::ctx->server_insert_cache(server);

  return server;
}

void Server::setUID(const QUuid &uid) {
  QWriteLocker locker(&mtx_lock);
  m_uid = uid;
  m_uid_str = uid.toString(QUuid::WithoutBraces).toUtf8();
}

QUuid Server::uid() const {
  QReadLocker locker(&mtx_lock);
  return m_uid;
}

QByteArray Server::name() const {
  QReadLocker locker(&mtx_lock);
  return m_name;
}

void Server::setName(const QByteArray &name) {
  QWriteLocker locker(&mtx_lock);
  const QByteArray old = m_name;
  m_name = name;
  emit nameChanged(old, name);
}

QSharedPointer<Account> Server::accountOwner() const {
  QReadLocker locker(&mtx_lock);
  return m_owner;
}

void Server::setAccountOwner(const QSharedPointer<Account> &owner) {
  QWriteLocker locker(&mtx_lock);
  m_owner = owner;
}

void Server::add_channel(const QSharedPointer<Channel> &channel) {
  QWriteLocker locker(&mtx_lock);
  m_channels[channel->uid] = channel;
}

void Server::remove_channel(const QSharedPointer<Channel> &channel) {
  QWriteLocker locker(&mtx_lock);
  m_channels.remove(channel->uid);
}

QList<QSharedPointer<Channel>> Server::all_channels() const {
  QReadLocker locker(&mtx_lock);
  return m_channels.values();
}

void Server::add_role(const QSharedPointer<Role> &role) {
  QWriteLocker locker(&mtx_lock);
  m_roles[role->uid()] = role;
}

void Server::remove_role(const QSharedPointer<Role> &role) {
  QWriteLocker locker(&mtx_lock);
  m_roles.remove(role->uid());
}

QSharedPointer<Role> Server::role_by_name(const QByteArray &name) const {
  QReadLocker locker(&mtx_lock);
  for (const auto &role : m_roles) {
    if (role->name() == name) {
      return role;
    }
  }
  return nullptr;
}

QList<QSharedPointer<Role>> Server::all_roles() const {
  QReadLocker locker(&mtx_lock);
  return m_roles.values();
}

bool Server::is_owned_by(const QUuid &account_uid) const {
  QReadLocker locker(&mtx_lock);
  return !m_owner.isNull() && m_owner->uid() == account_uid;
}

QSharedPointer<Server> Server::get_by_uid(const QUuid &uid) {
  QReadLocker locker(&g::ctx->mtx_cache);
  return g::ctx->servers_lookup_uuid.value(uid);
}

QSharedPointer<Server> Server::get_by_name(const QByteArray &name) {
  QReadLocker locker(&g::ctx->mtx_cache);
  return g::ctx->servers_lookup_name.value(name);
}

void Server::merge(const QSharedPointer<Server> &from) {
  QWriteLocker locker(&mtx_lock);

  for (auto &ch : from->m_channels)
    add_channel(ch);

  for (auto &role : from->m_roles)
    add_role(role);

  from->m_channels.clear();
  from->m_roles.clear();

  g::ctx->server_remove_cache(from);
}

QList<QSharedPointer<Account>> Server::all_accounts() const {
  QReadLocker locker(&mtx_lock);
  return m_accounts.values();
}

void Server::add_account(const QSharedPointer<Account> &acc) {
  if (acc.isNull()) return;
  QWriteLocker locker(&mtx_lock);
  m_accounts[acc->uid()] = acc;
}

void Server::remove_account(const QUuid &account_uid) {
  QWriteLocker locker(&mtx_lock);
  m_accounts.remove(account_uid);
}

QVariantMap Server::to_variantmap() const {
  QReadLocker locker(&mtx_lock);
  QVariantMap map;

  map["uid"] = m_uid;
  map["name"] = QString::fromUtf8(m_name);

  if (const auto owner = accountOwner()) {
    map["owner_uid"] = owner->uid();
  } else {
    map["owner_uid"] = QString(); // empty if no owner
  }

  map["creation_date"] = creation_date.toString(Qt::ISODate);

  // Channels
  QVariantList channelsList;
  for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
    channelsList.append(it.value()->uid);
  map["channels"] = channelsList;

  // Roles
  QVariantList rolesList;
  for (auto it = m_roles.begin(); it != m_roles.end(); ++it)
    rolesList.append(it.value()->uid());
  map["roles"] = rolesList;

  // Accounts
  QVariantList accountsList;
  for (auto it = m_accounts.begin(); it != m_accounts.end(); ++it)
    accountsList.append(it.value()->uid());
  map["accounts"] = accountsList;

  return map;
}

rapidjson::Value Server::to_rapidjson(
    rapidjson::Document::AllocatorType &allocator,
    bool include_channels,
    bool include_roles,
    bool include_accounts) const
{
  QReadLocker locker(&mtx_lock);
  rapidjson::Value obj(rapidjson::kObjectType);

  obj.AddMember("uid", rapidjson::Value(m_uid_str.constData(), allocator), allocator);
  obj.AddMember("name", rapidjson::Value(m_name.constData(), allocator), allocator);

  // Use owner account UID
  if (auto owner = accountOwner()) {
    obj.AddMember("owner_uid", rapidjson::Value(owner->uid_str().constData(), allocator), allocator);
  } else {
    obj.AddMember("owner_uid", rapidjson::Value("", allocator), allocator);
  }

  obj.AddMember("creation_date",
                rapidjson::Value(creation_date.toString(Qt::ISODate).toUtf8().constData(), allocator),
                allocator);

  if (include_channels) {
    rapidjson::Value channelsArray(rapidjson::kArrayType);
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
      channelsArray.PushBack(rapidjson::Value(it.value()->uid_str.constData(), allocator), allocator);
    obj.AddMember("channels", channelsArray, allocator);
  }

  if (include_roles) {
    rapidjson::Value rolesArray(rapidjson::kArrayType);
    for (auto it = m_roles.begin(); it != m_roles.end(); ++it)
      rolesArray.PushBack(rapidjson::Value(it.value()->uid_str().constData(), allocator), allocator);
    obj.AddMember("roles", rolesArray, allocator);
  }

  if (include_accounts) {
    rapidjson::Value accountsArray(rapidjson::kArrayType);
    for (auto it = m_accounts.begin(); it != m_accounts.end(); ++it)
      accountsArray.PushBack(rapidjson::Value(it.value()->uid_str().constData(), allocator), allocator);
    obj.AddMember("accounts", accountsArray, allocator);
  }

  return obj;
}

Server::~Server() {
  qDebug() << "RIP server" << m_name;
}