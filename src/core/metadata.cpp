// metadata.cpp
#include "metadata.h"
#include "core/account.h"
#include "core/channel.h"
#include <QDebug>

Metadata::Metadata(Account *account, QObject *parent) :
  m_account(account), QObject(parent) {
}

Metadata::Metadata(Channel *channel, QObject *parent) :
  m_channel(channel), QObject(parent) {
}

void Metadata::set(const QString &key, const QVariant &value, const QSharedPointer<Account> &actor) {
  kv[key] = value;
  emit changed(key, value);
  // TODO: later: notify subscribers
}

void Metadata::handle(const QSharedPointer<QEventMetadata> &event) {
  const QString cmd = QString::fromUtf8(event->subcmd).toUpper();
  const QList<QByteArray> &args = event->args;

  // set metadata
  if (cmd == "SET") {
    if (args.size() < 2) return;

    auto& key = args[0];
    auto& value = args[1];

    if (event->account != m_account) {
      event->error_code = "KEY_NO_PERMISSION";
      event->error_target = m_account ? m_account->nick() : ("#" + m_channel->name());
      event->error_key = key;
      return;
    }

    kv[key] = value;
    event->metadata[key] = value;
    return;
  }

  // clear metadata
  if (cmd == "CLEAR") {
    if (args.isEmpty()) return;

    auto& key = args[0];

    if (event->account != m_account) {
      event->error_code = "KEY_NO_PERMISSION";
      event->error_target = m_account ? m_account->nick() : ("#" + m_channel->name());
      event->error_key = key;
      return;
    }

    kv.remove(key);
    event->metadata[key] = QVariant();
    return;
  }

  // list or get metadata
  if (cmd == "LIST" || cmd == "GET") {
    for (auto it = kv.constBegin(); it != kv.constEnd(); ++it) {
      event->metadata[it.key()] = it.value();
    }
    return;
  }

  // subscribe to metadata keys
  if (cmd == "SUB") {
    for (const auto &a : args) {
      QByteArray key = a;
      subscribers[key].insert(event->account);
      event->subscriptions[key].insert(event->account);
    }
    return;
  }

  // unsubscribe from metadata keys
  if (cmd == "UNSUB") {
    for (const auto &a : args) {
      QByteArray key = a;
      if (subscribers.contains(key)) {
        subscribers[key].remove(event->account);
        event->subscriptions[key].remove(event->account);
      }
    }
    return;
  }

  // list subscriptions for the actor
  if (cmd == "SUBS") {
    for (auto it = subscribers.constBegin(); it != subscribers.constEnd(); ++it) {
      if (it.value().contains(event->account)) {
        event->subscriptions[it.key()].insert(event->account);
      }
    }
    return;
  }
}

QVariant Metadata::get(const QString &key) const {
  return kv.value(key, QVariant());
}

QMap<QString, QVariant> Metadata::list() const {
  return kv;
}

bool Metadata::clear(const QString &key, const QSharedPointer<Account> &actor) {
  if (!kv.contains(key))
    return false;
  kv.remove(key);
  emit changed(key, QVariant());
  return true;
}

void Metadata::sub(const QSharedPointer<Account> &actor, const QList<QString> &keys) {
  for (const auto &k: keys)
    subscribers[k].insert(actor);
}

void Metadata::unsub(const QSharedPointer<Account> &actor, const QList<QString> &keys) {
  for (const auto &k: keys) {
    if (subscribers.contains(k))
      subscribers[k].remove(actor);
  }
}

QSet<QString> Metadata::subs(const QSharedPointer<Account> &actor) const {
  QSet<QString> result;
  for (auto it = subscribers.constBegin(); it != subscribers.constEnd(); ++it) {
    if (it.value().contains(actor))
      result.insert(it.key());
  }
  return result;
}
