#pragma once
#include <QString>
#include <QSet>
#include <QByteArray>
#include <QPointer>

#include "core/account.h"
#include "irc/modes.h"

class Account;

class Channel final : public QObject {
  Q_OBJECT

public:
  explicit Channel(const QByteArray &name, QObject *parent = nullptr);
  static QSharedPointer<Channel> create_from_db(const QByteArray &id, const QByteArray &name, const QByteArray &topic, const QByteArray &ownerId, const QDateTime &creation);

  [[nodiscard]] bool has(const QByteArray &account_name) const;
  void join(const QByteArray &account_name);
  void join(QSharedPointer<Account> &account);
  void part(QSharedPointer<Account> &account, const QByteArray &message = "");
  void leave(const QByteArray &account_name);

  void message(const QSharedPointer<Account> &account, const QByteArray &message);

  void setMode(irc::ChannelModes mode, bool adding, const QByteArray &arg = {});

  static QSharedPointer<Channel> get(const QByteArray &channel_name);
  static QSharedPointer<Channel> get_or_create(const QByteArray &channel_name);

  const QList<QSharedPointer<Account>> &members() const { return m_members; }
  void addMembers(QList<QSharedPointer<Account>> accounts);

  [[nodiscard]] QByteArray name() const { return m_name; }
  void setName(const QByteArray &name);

  [[nodiscard]] QByteArray topic() const { return m_topic; }
  void setTopic(const QByteArray &t);

  [[nodiscard]] QByteArray key() const { return m_key; }
  void setKey(const QByteArray &k);

  [[nodiscard]] QSharedPointer<Account> accountOwner() const;

  [[nodiscard]] QByteArray accountOwnerId() const { return m_account_owner_id; }
  void setAccountOwnerId(const QByteArray &uuidv4);

  Flags<irc::ChannelModes> channel_modes;

  void addBan(const QByteArray &mask);
  void removeBan(const QByteArray &mask);
  QList<QByteArray> banList() const;
  int limit() const { return m_limit; }

  QByteArray uid;
  QDateTime date_creation;

signals:
  void topicChanged(const QByteArray &newTopic);
  void keyChanged(const QByteArray &newKey);
  void accountOwnerIdChanged(const QByteArray &account_owner_id);

  void memberJoined(const QSharedPointer<Account> &account);
  void memberJoinedFailed(const QSharedPointer<Account> &account);
  void memberRemoved(const QSharedPointer<Account> &account);

public slots:
  void onNickChanged(const QSharedPointer<Account> &ptr, const QByteArray& old_nick, const QByteArray& new_nick, QSet<QSharedPointer<Account>> &broadcasted_accounts);

private:
  QByteArray m_name;
  QByteArray m_topic;
  QByteArray m_key;
  QByteArray m_account_owner_id;
  QList<QSharedPointer<Account>> m_members;

  // bans
  QSet<QByteArray> m_ban_masks;
  int m_limit = 0;
};
