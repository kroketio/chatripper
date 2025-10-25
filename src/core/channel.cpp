#include "core/channel.h"

#include "ctx.h"
#include "lib/globals.h"
#include "core/server.h"
#include "core/qtypes.h"

Channel::Channel(const QByteArray &name, QObject *parent) : QObject(parent), m_name(name) {
  channel_modes.set(
    irc::ChannelModes::NO_OUTSIDE_MSGS,
    irc::ChannelModes::TOPIC_PROTECTED);
}

bool Channel::has(const QByteArray &username) const {
  return true;
}

QSharedPointer<Channel> Channel::create_from_db(
  const QUuid &id,
  const QByteArray &name,
  const QByteArray &topic,
  const QSharedPointer<Account> &owner,
  const QSharedPointer<Server> &server,
  const QDateTime &creation
) {
  auto const ctx = Ctx::instance();
  const auto it = ctx->channels.find(name);
  if (it != ctx->channels.end())
    return it.value();

  auto channel = QSharedPointer<Channel>(new Channel(name));
  if (g::mainThread != QThread::currentThread())
    channel->moveToThread(g::mainThread);

  channel->uid = id;
  channel->uid_str = id.toString(QUuid::WithoutBraces).toUtf8();
  channel->setName(name);
  if (!owner.isNull())
    channel->setAccountOwner(owner);
  channel->setServer(server);
  channel->setTopic(topic);
  channel->date_creation = creation;

  ctx->channels[name] = channel;
  return channel;
}

QSharedPointer<Server> Channel::server() const {
  QReadLocker locker(&mtx_lock);
  return m_server;
}

void Channel::setServer(const QSharedPointer<Server> &server) {
  QWriteLocker locker(&mtx_lock);
  m_server = server;
}

bool Channel::part(const QSharedPointer<QEventChannelPart> &event) {
  if (!event->from_system && g::ctx->snakepit->hasEventHandler(QEnums::QIRCEvent::CHANNEL_PART)) {
    const auto result = g::ctx->snakepit->event(QEnums::QIRCEvent::CHANNEL_PART, event);

    if (result.canConvert<QSharedPointer<QEventChannelPart>>()) {
      auto resPtr = result.value<QSharedPointer<QEventChannelPart>>();
      if (resPtr->cancelled())
        return false;
    }
  }

  const auto chan_ptr = get(m_name);

  QReadLocker rlock(&mtx_lock);
  if (!m_members.contains(event->account))
    return false;
  rlock.unlock();

  // broadcast
  rlock.relock();
  for (const auto& member: m_members) {
    for (const auto& conn: member->connections) {
      qDebug() << "emit part to" << member->name();

      QMetaObject::invokeMethod(conn,
        [conn, event] {
          conn->channel_part(event);
        }, Qt::QueuedConnection);
    }
  }
  rlock.unlock();

  QWriteLocker wlock(&mtx_lock);
  m_members.removeAll(event->account);
  wlock.unlock();

  return true;
}

// @TODO: check if user is allowed to join/create new channel
void Channel::join(const QSharedPointer<QEventChannelJoin> &event) {
  const auto chan_ptr = get(m_name);

  if (!event->from_system && g::ctx->snakepit->hasEventHandler(QEnums::QIRCEvent::CHANNEL_JOIN)) {
    const auto result = g::ctx->snakepit->event(QEnums::QIRCEvent::CHANNEL_JOIN, event);

    if (result.canConvert<QSharedPointer<QEventChannelJoin>>()) {
      auto resPtr = result.value<QSharedPointer<QEventChannelJoin>>();
      if (resPtr->cancelled())
        return;
    }
  }

  QWriteLocker locker(&mtx_lock);
  if (!m_members.contains(event->account)) {
    m_members << event->account;
    emit memberJoined(event->account);
    event->account->channels[m_name] = chan_ptr;
  }

  // make sure the various connections are actually in this channel
  for (const auto& conn: event->account->connections) {
    if (!conn->channels.contains(m_name))
      conn->channel_join(event);
  }

  // notify channel participants
  for (const auto& member: m_members) {
    if (member->uid() == event->account->uid())
      continue;

    for (const auto& conn: member->connections) {
      if (!conn->channel_members[chan_ptr].contains(event->account)) {
        conn->channel_join(event);
      }
    }
  }
}

void Channel::setTopic(const QByteArray &t) {
  QWriteLocker locker(&mtx_lock);
  if (m_topic != t) {
    m_topic = t;
    emit topicChanged(m_topic);
  }
}

void Channel::setName(const QByteArray &name) {
  QWriteLocker locker(&mtx_lock);
  m_name = name;
}

void Channel::setKey(const QByteArray &k) {
  QWriteLocker locker(&mtx_lock);
  if (m_key != k) {
    m_key = k;
    emit keyChanged(m_key);
  }
}

QSharedPointer<Channel> Channel::get(const QByteArray &channel_name) {
  return g::ctx->channels.value(channel_name);
}

QSharedPointer<Channel> Channel::get_or_create(const QByteArray &channel_name) {
  auto &channels = g::ctx->channels;
  if (const auto it = channels.find(channel_name); it != channels.end())
    return it.value();

  // @TODO: check if we are allowed to do this according to permissions
  const auto channel = new Channel(channel_name);

  if (g::mainThread != QThread::currentThread())
    channel->moveToThread(g::mainThread);

  auto ptr = QSharedPointer<Channel>(channel);

  channels.insert(channel_name, ptr);
  return ptr;
}

QSharedPointer<Account> Channel::accountOwner() const {
  QReadLocker locker(&mtx_lock);
  return m_owner;
}

void Channel::setAccountOwner(const QSharedPointer<Account> &owner) {
  QWriteLocker locker(&mtx_lock);
  m_owner = owner;
}

void Channel::addMembers(QList<QSharedPointer<Account>> accounts) {
  QWriteLocker locker(&mtx_lock);
  for (const auto& acc: accounts) {
    m_members.append(acc);
    acc->channels[m_name] = g::ctx->channels[m_name];
  }
}

void Channel::addBan(const QByteArray &mask) {
  QWriteLocker locker(&mtx_lock);
  if (!mask.isEmpty()) {
    m_ban_masks.insert(mask);
    // TODO: persist or notify members if desired
  }
}

void Channel::removeBan(const QByteArray &mask) {
  QWriteLocker locker(&mtx_lock);
  if (!mask.isEmpty()) {
    m_ban_masks.remove(mask);
    // TODO: persist or notify members if desired
  }
}

QList<QByteArray> Channel::banList() const {
  QReadLocker locker(&mtx_lock);
  return m_ban_masks.values();
}

// @TODO: check if user is actually online, store stuff in db if not
void Channel::message(QSharedPointer<QEventMessage> &message) {
  auto ev_type = QEnums::QIRCEvent::CHANNEL_MSG;
  if (message->tag_msg)
    ev_type = QEnums::QIRCEvent::TAG_MSG;

  if (g::ctx->snakepit->hasEventHandler(ev_type)) {
    const auto result = g::ctx->snakepit->event(ev_type, message);

    if (result.canConvert<QSharedPointer<QEventMessage>>()) {
      const auto resPtr = result.value<QSharedPointer<QEventMessage>>();
      if (resPtr->cancelled())
        return;
    } else {
      return;
    }
  }

  sql::insert_message(message);

  QReadLocker locker(&mtx_lock);
  for (const auto&member: m_members) {
    for (const auto& _conn: member->connections) {
      _conn->message(message);
    }
  }
}

// @TODO:
// 1. throttle channel renames  (:irc.example.com FAIL RENAME CANNOT_RENAME #magical-girls #witches :This channel has been renamed recently)
// 2. register channel redirects in db
// 3. test if this actually works
// 4. check permissions if user may rename
bool Channel::rename(const QSharedPointer<QEventChannelRename> &event) {
  if (event->old_name == event->new_name)
    return false;

  const auto channel_from = get(event->channel->name());
  if (channel_from.isNull())
    return false;

  if (g::ctx->snakepit->hasEventHandler(QEnums::QIRCEvent::CHANNEL_RENAME)) {
    const auto result = g::ctx->snakepit->event(
      QEnums::QIRCEvent::CHANNEL_RENAME,
      event);

    if (result.canConvert<QSharedPointer<QEventNickChange>>()) {
      const auto resPtr = result.value<QSharedPointer<QEventNickChange>>();
      if (resPtr->cancelled())
        return false;
    }
  }

  channel_from->setName(event->new_name);

  // broadcast
  for (const auto& acc: event->channel->members()) {
    for (const auto&conn : acc->connections) {
      QMetaObject::invokeMethod(conn,
        [conn, event] {
          conn->channel_rename(event);
        }, Qt::QueuedConnection);
    }
  }

  return true;
}

void Channel::setMode(irc::ChannelModes mode, bool adding, const QByteArray &arg) {
  QWriteLocker locker(&mtx_lock);
  using irc::ChannelModes;

  switch (mode) {
    // modes without extra arguments
    case ChannelModes::INVITE_ONLY:
    case ChannelModes::MODERATED:
    case ChannelModes::NO_OUTSIDE_MSGS:
    case ChannelModes::QUIET:
    case ChannelModes::SECRET:
    case ChannelModes::TOPIC_PROTECTED: {
      if (adding) channel_modes.set(mode);
      else channel_modes.clear(mode);
      break;
    }

    // +k (key)
    case ChannelModes::KEY: {
      if (adding) {
        setKey(arg);
        channel_modes.set(mode);
      } else {
        setKey({});
        channel_modes.clear(mode);
      }
      break;
    }

    // +l (limit)
    case ChannelModes::LIMIT: {
      if (adding) {
        bool ok = false;
        const int newLimit = arg.toInt(&ok);
        if (ok && newLimit >= 0) {
          m_limit = newLimit;
          channel_modes.set(mode);
        } else {
          // invalid argument -> ignore or log
        }
      } else {
        m_limit = 0;
        channel_modes.clear(mode);
      }
      break;
    }

    // +b (ban masks) - usually stored as a list
    case ChannelModes::BAN: {
      if (adding) {
        if (!arg.isEmpty()) addBan(arg);
      } else {
        if (!arg.isEmpty()) removeBan(arg);
      }
      break;
    }

    default:
      break;
  } // switch
}
