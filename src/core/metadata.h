#pragma once
#include <QObject>
#include <QMap>
#include <QReadWriteLock>
#include <QSet>
#include <QSharedPointer>
#include <QVariant>

#include "core/qtypes.h"

class Account;
class Channel;

class Metadata final : public QObject {
  Q_OBJECT
public:
  explicit Metadata(Account *account, QObject *parent = nullptr);
  explicit Metadata(Channel *channel, QObject *parent = nullptr);

  QMap<QString, QVariant> kv;

  // @TODO: needs testing
  QMap<QString, QSet<QSharedPointer<Account>>> subscribers;

  // core
  void set(const QByteArray &key, const QByteArray &value);
  bool remove(const QByteArray &key);
  QVariant get(const QByteArray &key) const;
  QMap<QString, QVariant> list() const;

  // subs
  void sub(const QSharedPointer<Account> &actor, const QList<QByteArray> &keys);
  void unsub(const QSharedPointer<Account> &actor, const QList<QByteArray> &keys);
  QSet<QString> subs(const QSharedPointer<Account> &actor) const;

  void handle(const QSharedPointer<QEventMetadata> &event);

  signals:
    void changed(const QString &key, const QVariant &value);

  private:
    mutable QReadWriteLock mtx_lock;
    QSharedPointer<Account> m_account;
    QSharedPointer<Channel> m_channel;
    QUuid ref_id() const;
};
