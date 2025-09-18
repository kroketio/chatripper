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
  if (!uid.isEmpty())
    throw std::runtime_error("Random UID should be empty");

  const QUuid uuid = QUuid::createUuid();
  uid = uuid.toRfc4122();
}

bool Account::verifyPassword(const QByteArray &candidate) const {
  if (candidate.isEmpty() || password.isEmpty())
    return false;

  const std::string candidateStr = candidate.toStdString();
  const std::string pw = password.toStdString();
  return bcrypt::validatePassword(candidateStr, pw);
}

QSharedPointer<Account> Account::create_from_db(const QByteArray &id, const QByteArray &username, const QByteArray &password, const QDateTime &creation) {
  auto const ctx = Ctx::instance();
  const auto it = ctx->accounts_lookup_name.find(username);
  if (it != ctx->accounts_lookup_name.end()) {
    auto ptr = it.value();
    return ptr;
  }

  QSharedPointer<Account> account = QSharedPointer<Account>::create(username);
  account->uid = id;
  account->setName(username);
  account->password = password;
  account->creation_date = creation;

  ctx->accounts << account;
  ctx->accounts_lookup_name[account->name()] = account;
  ctx->accounts_lookup_uuid[account->uid] = account;

  return account;
}

QByteArray Account::name() {
  return m_name;
}

void Account::setHost(const QByteArray &host) {
  m_host = host;
}

void Account::setName(const QByteArray &name) {
  m_name = name;
}

QByteArray Account::nick() {
  if (!m_nick.isEmpty())
    return m_nick;

  for (const auto& conn: connections) {
    return conn->nick;
  }

  return {};
}

bool Account::setNick(const QByteArray &nick) {
  if (nick.isEmpty())
    return false;

  const auto self = g::ctx->accounts_lookup_uuid.value(uid);

  const QByteArray nick_lower = nick.toLower();
  const QByteArray nick_old = m_nick;
  const QByteArray nick_old_lower = m_nick.toLower();

  if (g::ctx->irc_nicks.contains(nick_lower) && g::ctx->irc_nicks.value(nick_lower) != self)
    // @TODO: better return errors
    return false;

  g::ctx->irc_nicks.remove(nick_old);
  g::ctx->irc_nicks[nick_lower] = self;

  m_nick = nick;

  // emit nickChanged(nick_old, nick);

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
    if (acc->uid == uid) {
      // skip
    } else {
      for (const auto&conn : acc->connections) {
        conn->change_nick(self, nick_old, nick);
      }
    }
  }
  //

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

  const auto ptr = get_by_uid(uid);
  for (const auto& _conn: dest->connections)
    _conn->message(ptr, m_nick, message);

  // send to ourselves (other connected clients)
  for (const auto& _conn: connections) {
    if (conn == _conn)
      continue;
    _conn->self_message(m_nick, message);
  }
}

void Account::broadcast_nick_changed(const QByteArray& msg) const {
  for (const auto& conn: connections) {
    conn->m_socket->write(msg);
  }
}

void Account::add_connection(irc::client_connection *ptr) {
  connect(ptr, &irc::client_connection::disconnected, [=] {
    connections.removeAll(ptr);

    // when unregistered, we need to clean the global account roster
    if (!is_logged_in()) {
      g::ctx->irc_nicks.remove(m_nick);

      if (const auto __ptr = g::ctx->accounts_lookup_uuid.value(uid); !__ptr.isNull()) {
        g::ctx->accounts.remove(__ptr);
        g::ctx->accounts_lookup_uuid.remove(uid);
      }
    }
  });

  connect(ptr, &QObject::destroyed, this, [this, ptr] {
    connections.removeAll(ptr);
  });

  connections << ptr;
}

QSharedPointer<Account> Account::get_by_uid(const QByteArray &uid) {
  return g::ctx->accounts_lookup_uuid.value(uid);
}

QSharedPointer<Account> Account::get_by_name(const QByteArray &name) {
  return g::ctx->accounts_lookup_name.value(name);
}

QSharedPointer<Account> Account::get_or_create(const QByteArray &account_name) {
  // @TODO: slow qlist
  for (const auto& ptr: g::ctx->accounts) {
    if (ptr->name() == account_name)
      return ptr;
  }

  const auto account = new Account(account_name);
  auto ptr = QSharedPointer<Account>(account);
  g::ctx->accounts << ptr;
  return ptr;
}

// account merging; we consume account `from` and adopt its connections
void Account::merge(const QSharedPointer<Account> &from) {
  if (from->is_logged_in()) {
    qCritical() << "cannot merge 2 logged in accounts";
    return;
  }

  for (const auto& conn: from->connections)
    add_connection(conn);

  g::ctx->accounts_lookup_uuid.remove(from->uid);
  g::ctx->accounts.remove(from);

  // @TODO: maybe update the db, update message authors.. but probably not
}

Account::~Account() {
  qDebug() << "RIP account" << m_name;
}

void Account::add_channel(const QByteArray& channel) {
  qDebug() << "account" << m_name << "add channel" << channel;
}