#include "core/channel.h"
#include "irc/client_connection.h"

#include "ctx.h"
#include "lib/globals.h"

Channel::Channel(const QByteArray &name, QObject *parent) : QObject(parent), m_name(name) {}

bool Channel::has(const QByteArray &username) const {
  return true;
}

QSharedPointer<Channel> Channel::create_from_db(const QByteArray &id, const QByteArray &name, const QByteArray &ownerId, const QDateTime &creation) {
  auto const ctx = Ctx::instance();
  const auto it = ctx->channels.find(name);
  if (it != ctx->channels.end())
    return it.value();

  QSharedPointer<Channel> channel = QSharedPointer<Channel>::create(name);
  channel->uid = id;
  channel->setName(name);
  channel->setAccountOwnerId(ownerId);
  channel->date_creation = creation;

  ctx->channels[name] = channel;
  return channel;
}

void Channel::join(QSharedPointer<Account> &account) {
  const auto chan_ptr = get_or_create(m_name);

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

  // let channel participants know
  for (const auto& member: m_members) {
    if (member->uid == account->uid) continue;

    for (const auto& conn: member->connections) {
      if (!conn->channel_members[chan_ptr].contains(account)) {
        auto acc_prefix = account->prefix(0);

        // @TODO: move IRC related code to client_connection
        const QByteArray msg = ":" + acc_prefix + " JOIN :#" + this->name() + "\r\n";
        conn->m_socket->write(msg);
      }
    }
  }

}

void Channel::join(const QByteArray &account_name) {

}

void Channel::leave(const QByteArray &username) {
  // if (m_members.remove(ptr) > 0) {
    // emit memberRemoved(ptr);
  // }
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
  return g::ctx->accounts_lookup_uuid.value(m_account_owner_id);
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
