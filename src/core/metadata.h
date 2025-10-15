#pragma once
#include <QObject>
#include <QMap>
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
  void set(const QString &key, const QVariant &value, const QSharedPointer<Account> &actor);
  QVariant get(const QString &key) const;
  QMap<QString, QVariant> list() const;
  bool clear(const QString &key, const QSharedPointer<Account> &actor);

  // subs
  void sub(const QSharedPointer<Account> &actor, const QList<QString> &keys);
  void unsub(const QSharedPointer<Account> &actor, const QList<QString> &keys);
  QSet<QString> subs(const QSharedPointer<Account> &actor) const;

  void handle(const QSharedPointer<QEventMetadata> &event);

  signals:
    void changed(const QString &key, const QVariant &value);

  private:
    QSharedPointer<Account> m_account;
    QSharedPointer<Channel> m_channel;
};
