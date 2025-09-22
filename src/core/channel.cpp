#include "core/channel.h"

#include "ctx.h"
#include "lib/globals.h"

Channel::Channel(const QByteArray &name, QObject *parent) : QObject(parent), m_name(name) {
  channel_modes.set(
    irc::ChannelModes::NO_OUTSIDE_MSGS,
    irc::ChannelModes::TOPIC_PROTECTED
  );
}

bool Channel::has(const QByteArray &username) const {
  return true;
}

QSharedPointer<Channel> Channel::create_from_db(const QByteArray &id, const QByteArray &name, const QByteArray &topic, const QByteArray &ownerId, const QDateTime &creation) {
  auto const ctx = Ctx::instance();
  const auto it = ctx->channels.find(name);
  if (it != ctx->channels.end())
    return it.value();

  QSharedPointer<Channel> channel = QSharedPointer<Channel>::create(name);
  channel->uid = id;
  channel->setName(name);
  channel->setAccountOwnerId(ownerId);
  channel->setTopic(topic);
  channel->date_creation = creation;

  ctx->channels[name] = channel;
  return channel;
}

void Channel::part(QSharedPointer<Account> &account, const QByteArray &message) {
  const auto chan_ptr = get(m_name);
  if (!m_members.contains(account))
    return;

  // part the various connections
  for (const auto& conn: account->connections) {
    if (conn->channels.contains(m_name))
      conn->channel_part(m_name, message);
  }

  // notify channel participants
  for (const auto& member: m_members) {
    if (member->uid() == account->uid()) continue;

    for (const auto& conn: member->connections) {
      if (conn->channel_members[chan_ptr].contains(account)) {
        conn->channel_part(account, chan_ptr, message);
      }
    }
  }
}

void Channel::join(QSharedPointer<Account> &account) {
  const auto chan_ptr = get(m_name);

  if (!m_members.contains(account)) {
    m_members << account;
    emit memberJoined(account);
    account->channels[m_name] = chan_ptr;
  }

  // make sure the various connections are actually in this channel
  for (const auto& conn: account->connections) {
    if (!conn->channels.contains(m_name))
      conn->channel_join(m_name, "");
  }

  // notify channel participants
  for (const auto& member: m_members) {
    if (member->uid() == account->uid()) continue;

    for (const auto& conn: member->connections) {
      if (!conn->channel_members[chan_ptr].contains(account)) {
        conn->channel_join(chan_ptr, account, "");
      }
    }
  }
}

void Channel::setTopic(const QByteArray &t) {
  if (m_topic != t) {
    m_topic = t;
    emit topicChanged(m_topic);
  }
}

void Channel::setAccountOwnerId(const QByteArray &uuidv4) {
  m_account_owner_id = uuidv4;
  emit accountOwnerIdChanged(uuidv4);
}

void Channel::setName(const QByteArray &name) {
  m_name = name;
}

void Channel::setKey(const QByteArray &k) {
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

  const auto channel = new Channel(channel_name);

  auto ptr = QSharedPointer<Channel>(channel);

  channels.insert(channel_name, ptr);
  return ptr;
}

QSharedPointer<Account> Channel::accountOwner() const {
  if (m_account_owner_id.isNull())
    return {};
  return Account::get_by_uid(m_account_owner_id);
}

void Channel::onNickChanged(const QSharedPointer<Account> &acc, const QByteArray& old_nick, const QByteArray& new_nick, QSet<QSharedPointer<Account>> &broadcasted_accounts) {
  for (const auto&_acc: m_members) {
    if (_acc == acc)
      continue;

    if (broadcasted_accounts.contains(_acc)) {
      continue;
    }

    for (const auto& conn: _acc->connections) {
      broadcasted_accounts << _acc;
      conn->change_nick(acc, old_nick, new_nick);
    }
  }
}

void Channel::addMembers(QList<QSharedPointer<Account>> accounts) {
  for (const auto& acc: accounts) {
    m_members.append(acc);
    acc->channels[m_name] = g::ctx->channels[m_name];

    // connect(acc.data(), &Account::nickChanged, [acc, this](const QByteArray &old_nick, const QByteArray &new_nick) {
    //   // this->onNickChanged(acc, old_nick, new_nick);
    // });
  }
}

void Channel::addBan(const QByteArray &mask) {
  if (!mask.isEmpty()) {
    m_ban_masks.insert(mask);
    // TODO: persist or notify members if desired
  }
}

void Channel::removeBan(const QByteArray &mask) {
  if (!mask.isEmpty()) {
    m_ban_masks.remove(mask);
    // TODO: persist or notify members if desired
  }
}

QList<QByteArray> Channel::banList() const {
  return m_ban_masks.values();
}

void Channel::message(const irc::client_connection *from_conn, const QSharedPointer<Account> &from, QSharedPointer<QMessage> &message) {
  if (g::ctx->snakepit->hasEventHandler(QIRCEvent::CHANNEL_MSG)) {
    auto res = g::ctx->snakepit->event(QIRCEvent::CHANNEL_MSG, this->to_variantmap(), from->to_variantmap(), message->to_variantmap());
    if (res.canConvert<QMessage>()) {
      const auto new_msg = res.value<QMessage>();
      // @TODO: we can do better
      message->id = new_msg.id;
      message->tags = new_msg.tags;
      message->nick = new_msg.nick;
      message->user = new_msg.user;
      message->host = new_msg.host;
      message->targets = new_msg.targets;
      message->account = new_msg.account;
      message->text = new_msg.text;
      message->raw = new_msg.raw;
      message->from_server = new_msg.from_server;
    }
  }

  // @TODO: check if user is actually online, store stuff in db if not

  for (const auto&member: m_members) {
    if (member != from) {
      for (const auto& _conn: member->connections)
        _conn->message(from, "#" + m_name, message);
    } else {  // ourselves
      for (const auto& _conn: from->connections) {
        if (_conn != from_conn)
          _conn->self_message("#" + m_name, message);  // ZNC_SELF_MESSAGE  @TODO: remove # nonsense
        else
          continue;  // @TODO: echo-message
      }
    }
  }
}

void Channel::setMode(irc::ChannelModes mode, bool adding, const QByteArray &arg) {
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
