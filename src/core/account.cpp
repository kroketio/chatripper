#include <QObject>
#include <QHostAddress>
#include <QDateTime>

#include "bcrypt/bcrypt.h"
#include "lib/globals.h"
#include "core/qtypes.h"
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
  m_uid_str = Utils::uuidBytesToString(m_uid).toUtf8();
}

QSharedPointer<QEventAuthUser> Account::verifyPassword(const QSharedPointer<QEventAuthUser> &auth) const {
  QReadLocker locker(&mtx_lock);

  if (auth->password.isEmpty() || m_password.isEmpty()) {
    auth->reason = "password cannot be empty";
    auth->_cancel = true;
    return auth;
  }

  if (g::ctx->snakepit->hasEventHandler(QEnums::QIRCEvent::AUTH_SASL_PLAIN)) {
    const auto result = g::ctx->snakepit->event(
      QEnums::QIRCEvent::AUTH_SASL_PLAIN,
      auth);

    if (result.canConvert<QSharedPointer<QEventAuthUser>>()) {
      auto resPtr = result.value<QSharedPointer<QEventAuthUser>>();
      return resPtr;
    }

    auth->reason = "application error";
    auth->_cancel = true;
    return auth;
  }

  const std::string candidateStr = auth->password.toStdString();
  const std::string pw = m_password.toStdString();
  auth->_cancel = !bcrypt::validatePassword(candidateStr, pw);
  auth->reason = auth->cancelled() ? "bad password" : "";
  return auth;
}

QSharedPointer<Account> Account::create() {
  auto account = QSharedPointer<Account>(new Account());
  if (g::mainThread != QThread::currentThread())
    account->moveToThread(g::mainThread);
  return account;
}

QSharedPointer<Account> Account::create_from_db(const QByteArray &id, const QByteArray &username, const QByteArray &password, const QDateTime &creation) {
  if (const auto ptr = get_by_name(username); !ptr.isNull())
    return ptr;

  auto account = QSharedPointer<Account>(new Account(username));
  if (g::mainThread != QThread::currentThread())
    account->moveToThread(g::mainThread);

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
  m_uid_str = Utils::uuidBytesToString(m_uid).toUtf8();
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
    return conn->nick();
  }

  return {};
}

bool Account::setNick(const QByteArray &nick) {
  if (nick.isEmpty())
    return false;

  const auto self = get_by_uid(m_uid);
  if (self.isNull())
    return false;

  QByteArray nick_lower;
  QByteArray nick_old;
  QByteArray nick_old_lower;

  {
    QReadLocker rlock(&mtx_lock);
    nick_lower = nick.toLower();
    nick_old = m_nick;
    nick_old_lower = m_nick.toLower();
  }

  const auto irc_nick_ptr = g::ctx->irc_nick_get(nick_lower);
  QWriteLocker mtx_irc_nick_rlock(&g::ctx->mtx_cache);
  if (!irc_nick_ptr.isNull() && g::ctx->irc_nicks.value(nick_lower) != self)
    // @TODO: better return errors
    return false;
  mtx_irc_nick_rlock.unlock();

  QWriteLocker mtx_irc_nick(&g::ctx->mtx_cache);
  g::ctx->irc_nicks.remove(nick_old);
  g::ctx->irc_nicks[nick_lower] = self;
  mtx_irc_nick.unlock();

  QWriteLocker wlock(&mtx_lock);
  m_nick = nick;
  wlock.unlock();

  // gather accounts that need to be notified
  QSet<QSharedPointer<Account>> l;

  {
    QReadLocker rlock(&mtx_lock);
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
          QMetaObject::invokeMethod(conn,
            [conn, self, nick_old, nick] {
              conn->change_nick(self, nick_old, nick);
            }, Qt::QueuedConnection);
        }
      }
    }

    for (const auto& conn: connections) {
      QMetaObject::invokeMethod(conn,
        [=] {
          conn->change_nick(m_nick);
        }, Qt::QueuedConnection);
    }
  }

  return true;
}

void Account::message(const irc::client_connection *conn, const QSharedPointer<Account> &dest, QSharedPointer<QEventMessage> &message) {
  // @TODO: deal with history when we are offline

  if (g::ctx->snakepit->hasEventHandler(QEnums::QIRCEvent::PRIVATE_MSG)) {
    const auto result = g::ctx->snakepit->event(
      QEnums::QIRCEvent::PRIVATE_MSG,
      message);

    if (result.canConvert<QSharedPointer<QEventMessage>>()) {
      const auto resPtr = result.value<QSharedPointer<QEventMessage>>();
      if (resPtr->cancelled())
        return;
    }
  }

  const auto self = get_by_uid(m_uid);
  if (self.isNull())
    return;

  QReadLocker locker(&mtx_lock);
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

void Account::onConnectionDisconnected(irc::client_connection *conn, const QByteArray& nick_to_delete) {
  QWriteLocker locker(&mtx_lock);
  connections.removeAll(conn);
  locker.unlock();

  // when unregistered, we need to clean the global account roster
  if (!is_logged_in()) {
    g::ctx->irc_nicks_remove_cache(nick_to_delete);

    const auto self = get_by_uid(uid());
    if (self.isNull())
      return;

    g::ctx->account_remove_cache(self);
  }
}

void Account::add_connection(irc::client_connection *ptr) {
  connections << ptr;
}

void Account::clearConnections() {
  connections.clear();
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

  from->clearConnections();
  g::ctx->account_remove_cache(from);

  // @TODO: maybe update the db, update message authors.. but probably not
}

Account::~Account() {
  qDebug() << "RIP account" << m_name;
}

void Account::add_channel(const QByteArray& channel) {
  qDebug() << "account" << m_name << "add channel" << channel;
}

QVariantMap Account::to_variantmap() const {
  QReadLocker locker(&mtx_lock);

  QVariantMap map;
  map["uid"] = QString::fromUtf8(m_uid);
  map["name"] = QString::fromUtf8(m_name);
  map["nick"] = QString::fromUtf8(m_nick);
  map["host"] = QString::fromUtf8(m_host.isEmpty() ? g::defaultHost : m_host);
  map["creation_date"] = creation_date.toString(Qt::ISODate);

  // Channels: store only the channel names the account is part of
  QVariantList channelList;
  for (auto it = channels.begin(); it != channels.end(); ++it) {
    if (it.value()) {
      channelList.append(QString::fromUtf8(it.value()->uid));
    }
  }

  map["channels"] = channelList;
  map["connections_count"] = connections.size();

  return map;
}

rapidjson::Value Account::to_rapidjson(rapidjson::Document::AllocatorType& allocator, bool include_channels, bool include_connection_count) const {
  QReadLocker locker(&mtx_lock);

  rapidjson::Value obj(rapidjson::kObjectType);

  // strings
  obj.AddMember("uid", rapidjson::Value(m_uid_str.constData(), allocator), allocator);
  obj.AddMember("name", rapidjson::Value(m_name.constData(), allocator), allocator);
  obj.AddMember("nick", rapidjson::Value(m_nick.constData(), allocator), allocator);
  obj.AddMember("host", rapidjson::Value((m_host.isEmpty() ? g::defaultHost : m_host).constData(), allocator), allocator);

  // date as string
  obj.AddMember("creation_date", rapidjson::Value(creation_date.toString(Qt::ISODate).toUtf8().constData(), allocator), allocator);

  if (include_channels) {
    // channels (only UID)
    rapidjson::Value channelsArray(rapidjson::kArrayType);
    for (auto it = channels.begin(); it != channels.end(); ++it) {
      if (it.value()) {
        channelsArray.PushBack(rapidjson::Value(it.value()->uid.constData(), allocator), allocator);
      }
    }
    obj.AddMember("channels", channelsArray, allocator);
  }

  if (include_connection_count)
    obj.AddMember("connections_count", static_cast<int>(connections.size()), allocator);

  return obj;
}
