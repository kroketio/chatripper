#include <QObject>
#include <QHostAddress>
#include <QDateTime>

#include "bcrypt/bcrypt.h"
#include "lib/globals.h"
#include "account.h"
#include "channel.h"
#include "ctx.h"

Account::Account(const QByteArray& account_name, QObject* parent) : m_name(account_name) ,QObject(parent) {
  qDebug() << "new account" << account_name;
}

void Account::setRandomUID() {
  QWriteLocker locker(&mtx_lock);
  if (!m_uid.isEmpty())
    throw std::runtime_error("Random UID should be empty");

  const QUuid uuid = QUuid::createUuid();
  m_uid = uuid.toRfc4122();
}

bool Account::verifyPassword(const QByteArray &candidate) const {
  QReadLocker locker(&mtx_lock);

  if (candidate.isEmpty() || m_password.isEmpty())
    return false;

  const std::string candidateStr = candidate.toStdString();
  const std::string pw = m_password.toStdString();
  return bcrypt::validatePassword(candidateStr, pw);
}

QSharedPointer<Account> Account::create_from_db(const QByteArray &id, const QByteArray &username, const QByteArray &password, const QDateTime &creation) {
  const auto it = g::ctx->accounts_lookup_name.find(username);
  if (it != g::ctx->accounts_lookup_name.end()) {
    auto ptr = it.value();
    return ptr;
  }

  QSharedPointer<Account> account = QSharedPointer<Account>::create(username);
  account->setUID(id);
  account->setName(username);
  account->setPassword(password);
  account->creation_date = creation;

  g::ctx->account_insert_cache(account);

  return account;
}

QByteArray Account::name() {
  QReadLocker locker(&mtx_lock);
  return m_name;
}

void Account::setHost(const QByteArray &host) {
  QWriteLocker locker(&mtx_lock);
  m_host = host;
}

void Account::setName(const QByteArray &name) {
  QWriteLocker locker(&mtx_lock);
  m_name = name;
}

QByteArray Account::uid() {
  QReadLocker locker(&mtx_lock);
  return m_uid;
}

void Account::setUID(const QByteArray &uid) {
  QWriteLocker locker(&mtx_lock);
  m_uid = uid;
}

QByteArray Account::password() {
  QReadLocker locker(&mtx_lock);
  return m_password;
}

void Account::setPassword(const QByteArray &password) {
  QWriteLocker locker(&mtx_lock);
  m_password = password;
}

QByteArray Account::nick() {
  QReadLocker locker(&mtx_lock);
  if (!m_nick.isEmpty())
    return m_nick;

  for (const auto& conn: connections) {
    return conn->nick;
  }

  return {};
}

bool Account::setNick(const QByteArray &nick) {
  QWriteLocker locker(&mtx_lock);
  if (nick.isEmpty())
    return false;

  const auto self = get_by_uid(m_uid);

  const QByteArray nick_lower = nick.toLower();
  const QByteArray nick_old = m_nick;
  const QByteArray nick_old_lower = m_nick.toLower();

  const auto irc_nick_ptr = g::ctx->irc_nick_get(nick_lower);
  if (!irc_nick_ptr.isNull() && g::ctx->irc_nicks.value(nick_lower) != self)
    // @TODO: better return errors
    return false;

  QWriteLocker mtx_irc_nick(&g::ctx->mtx_cache);
  g::ctx->irc_nicks.remove(nick_old);
  g::ctx->irc_nicks[nick_lower] = self;
  mtx_irc_nick.unlock();

  m_nick = nick;

  // gather accounts that need to be notified
  QSet<QSharedPointer<Account>> l;

  for (auto it = channels.begin(); it != channels.end(); ++it) {
    // QByteArray key = it.key();
    const QSharedPointer<Channel> &channel = it.value();
    for (const auto& acc: channel->members()) {
      l.insert(acc);
    }
  }

  for (const auto& channel: channels) {
    for (const auto& acc: channel->members()) {
      l.insert(acc);
    }
  }

  // send nick change notify to the relevant, other accounts
  for (const auto& acc: l) {
    if (acc != self) {
      for (const auto&conn : acc->connections) {
        conn->change_nick(self, nick_old, nick);
      }
    }
  }

  for (const auto& conn: connections) {
    conn->change_nick(m_nick);
    conn->nick = m_nick;
  }

  return true;
}

// @TODO: check if user is allowed to create new channel
void Account::channel_join(QSharedPointer<Account> &acc, const QByteArray& channel_name) {
  const auto ptr = Channel::get_or_create(channel_name);
  ptr->join(acc);
}

void Account::channel_part(QSharedPointer<Account> &acc, const QByteArray& channel_name, const QByteArray& message) {
  if (!g::ctx->channels.contains(channel_name))
    return;

  const auto ptr = Channel::get(channel_name);
  ptr->part(acc, message);
}

void Account::message(const irc::client_connection *conn, const QSharedPointer<Account> &dest, const QByteArray &message) {
  // @TODO: deal with history when we are offline
  QReadLocker locker(&mtx_lock);

  const auto self = get_by_uid(m_uid);
  for (const auto& _conn: dest->connections)
    _conn->message(self, m_nick, message);

  // send to ourselves (other connected clients)
  for (const auto& _conn: connections) {
    if (conn == _conn || self == dest)
      continue;
    _conn->self_message(dest->nick(), message);
  }
}

void Account::broadcast_nick_changed(const QByteArray& msg) const {
  QReadLocker locker(&mtx_lock);
  for (const auto& conn: connections) {
    conn->m_socket->write(msg);
  }
}

void Account::add_connection(irc::client_connection *ptr) {
  connect(ptr, &irc::client_connection::disconnected, [=] {
    QWriteLocker locker(&mtx_lock);
    connections.removeAll(ptr);
    locker.unlock();

    // when unregistered, we need to clean the global account roster
    if (!is_logged_in()) {
      const auto self = get_by_uid(uid());
      g::ctx->irc_nicks_remove_cache(nick());
      g::ctx->account_remove_cache(self);
    }
  });

  connect(ptr, &QObject::destroyed, this, [this, ptr] {
    QWriteLocker locker(&mtx_lock);
    connections.removeAll(ptr);
  });

  connections << ptr;
}

QSharedPointer<Account> Account::get_by_uid(const QByteArray &uid) {
  QReadLocker locker(&g::ctx->mtx_cache);
  return g::ctx->accounts_lookup_uuid.value(uid);
}

QSharedPointer<Account> Account::get_by_name(const QByteArray &name) {
  QReadLocker locker(&g::ctx->mtx_cache);
  return g::ctx->accounts_lookup_name.value(name);
}

// account merging; we consume account `from` and adopt its connections
void Account::merge(const QSharedPointer<Account> &from) {
  QWriteLocker locker(&mtx_lock);
  if (from->is_logged_in()) {
    qCritical() << "cannot merge 2 logged in accounts";
    return;
  }

  for (const auto& conn: from->connections)
    add_connection(conn);

  g::ctx->account_remove_cache(from);

  // @TODO: maybe update the db, update message authors.. but probably not
}

Account::~Account() {
  qDebug() << "RIP account" << m_name;
}

void Account::add_channel(const QByteArray& channel) {
  qDebug() << "account" << m_name << "add channel" << channel;
}