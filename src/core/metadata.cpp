// metadata.cpp
#include "metadata.h"
#include "core/account.h"
#include "core/channel.h"
#include "lib/sql.h"
#include <QDebug>

Metadata::Metadata(Account *account, QObject *parent) :
    m_account(account), QObject(parent) {
  auto [keyValues, subs] = sql::metadata_get(account->uid());
  kv = keyValues;
  subscribers = subs;
}

Metadata::Metadata(Channel *channel, QObject *parent) :
    m_channel(channel), QObject(parent) {
  auto [keyValues, subs] = sql::metadata_get(channel->uid);
  kv = keyValues;
  subscribers = subs;
}

QVariant Metadata::get(const QByteArray &key) const {
  QReadLocker rlock(&mtx_lock);
  return kv.value(key, QVariant());
}

bool Metadata::remove(const QByteArray &key) {
  QWriteLocker rlock(&mtx_lock);
  kv.remove(key);

  QUuid ref_id;
  if (!m_account.isNull()) {
    ref_id = m_account->uid();
  } else {
    ref_id = m_channel->uid;
  }

  sql::metadata_remove(key, ref_id);

  emit changed(key, QVariant());
  return true;
}

void Metadata::set(const QByteArray &key, const QByteArray &value) {
  QWriteLocker wlock(&mtx_lock);
  kv[key] = value;
  emit changed(key, value);

  QUuid ref_id;
  sql::RefType ref_type;
  if (!m_account.isNull()) {
    ref_id = m_account->uid();
    ref_type = sql::RefType::Account;
  } else {
    ref_id = m_channel->uid;
    ref_type = sql::RefType::Channel;
  }

  metadata_upsert(key, value, ref_id, ref_type);

  if (subscribers.contains(key)) {
    // TODO: later: notify subscribers
  }
}

void Metadata::handle(const QSharedPointer<QEventMetadata> &event) {
  const QString cmd = QString::fromUtf8(event->subcmd).toUpper();
  const QList<QByteArray> &args = event->args;

  if (cmd == "SET") {
    if (args.size() < 2) return;
    auto &key = args[0];
    auto &value = args[1];

    if (!m_account.isNull() && !event->account.isNull() && event->account != m_account) {
      event->error_code = "KEY_NO_PERMISSION";
      event->error_target = m_account->nick();
      event->error_key = key;
      return;
    }

    //  : ("#" + m_channel->name());

    set(key, value);
    event->metadata[key] = value;

    return;
  }

  if (cmd == "CLEAR") {
    if (args.isEmpty()) return;
    auto &key = args[0];

    if (!m_account.isNull() && !event->account.isNull() && event->account != m_account) {
      event->error_code = "KEY_NO_PERMISSION";
      event->error_target = m_account->nick();
      event->error_key = key;
      return;
    }

    remove(key);
    event->metadata[key] = QVariant();
    return;
  }

  if (cmd == "LIST") {
    QReadLocker rlock(&mtx_lock);
    for (auto it = kv.constBegin(); it != kv.constEnd(); ++it) {
      event->metadata[it.key()] = it.value();
    }
    return;
  }

  if (cmd == "GET") {
    QReadLocker rlock(&mtx_lock);

    // populate only requested keys
    for (const auto &key : args) {
      if (kv.contains(key)) {
        event->metadata[key] = kv[key];
      }
      // missing keys are left out; metadata() will respond with RPL_KEYNOTSET
    }
    return;
  }

  if (cmd == "SUB") {
    auto &keys = args;

    sub(event->account, keys);

    for (const auto&key : keys)
      event->subscriptions[key].insert(event->account);

    return;
  }

  if (cmd == "UNSUB") {
    auto &keys = args;

    unsub(event->account, keys);

    for (const auto&key : keys)
      event->subscriptions[key].insert(event->account);

    return;
  }

  if (cmd == "SUBS") {
    for (auto it = subscribers.constBegin(); it != subscribers.constEnd(); ++it) {
      if (it.value().contains(event->account)) {
        event->subscriptions[it.key()].insert(event->account);
      }
    }
    return;
  }
}

QMap<QString, QVariant> Metadata::list() const {
  QReadLocker rlock(&mtx_lock);
  return kv;
}

void Metadata::sub(const QSharedPointer<Account> &actor, const QList<QByteArray> &keys) {
  QWriteLocker rlock(&mtx_lock);

  for (const auto &k: keys)
    subscribers[k].insert(actor);

  QUuid ref_id;
  if (!m_account.isNull()) {
    ref_id = m_account->uid();
  } else {
    ref_id = m_channel->uid;
  }

  sql::metadata_subscribe_bulk(ref_id, keys, actor->uid());
}

void Metadata::unsub(const QSharedPointer<Account> &actor, const QList<QByteArray> &keys) {
  QWriteLocker rlock(&mtx_lock);

  QUuid ref_id;
  if (!m_account.isNull()) {
    ref_id = m_account->uid();
  } else {
    ref_id = m_channel->uid;
  }

  for (const auto &k: keys) {
    if (subscribers.contains(k))
      subscribers[k].remove(actor);
  }

  sql::metadata_unsubscribe_bulk(ref_id, keys, actor->uid());
}

QSet<QString> Metadata::subs(const QSharedPointer<Account> &actor) const {
  // ??
  QSet<QString> result;
  for (auto it = subscribers.constBegin(); it != subscribers.constEnd(); ++it) {
    if (it.value().contains(actor))
      result.insert(it.key());
  }
  return result;
}

QUuid Metadata::ref_id() const {
  if (!m_account.isNull())
    return m_account->uid();
  return m_channel->uid;
}
