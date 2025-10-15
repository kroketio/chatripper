#pragma once
#include <QString>
#include <QSet>
#include <QByteArray>
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>

#include "utils.h"
#include "core/account.h"
#include "irc/client_connection.h"
#include "irc/modes.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

class Account;
class Server;

class Channel final : public QObject {
Q_OBJECT
Q_PROPERTY(QByteArray name READ name WRITE setName NOTIFY topicChanged)
Q_PROPERTY(QByteArray topic READ topic WRITE setTopic NOTIFY topicChanged)
Q_PROPERTY(QByteArray key READ key WRITE setKey NOTIFY keyChanged)
Q_PROPERTY(int limit READ limit)
Q_PROPERTY(QDateTime date_creation MEMBER date_creation)
Q_PROPERTY(QByteArray uid MEMBER uid)
Q_PROPERTY(QByteArray uid_str MEMBER uid_str)

public:
  explicit Channel(const QByteArray &name, QObject *parent = nullptr);
  static QSharedPointer<Channel> create_from_db(
    const QByteArray &id,
    const QByteArray &name,
    const QByteArray &topic,
    const QSharedPointer<Account> &owner,
    const QSharedPointer<Server> &server,  // Server before creation
    const QDateTime &creation
  );

  [[nodiscard]] bool has(const QByteArray &account_name) const;
  // void join(const QByteArray &account_name);
  void join(const QSharedPointer<QEventChannelJoin> &event);
  bool part(const QSharedPointer<QEventChannelPart> &event);

  void setServer(const QSharedPointer<Server> &server);
  QSharedPointer<Server> server() const;

  void message(QSharedPointer<QEventMessage> &message);

  void setMode(irc::ChannelModes mode, bool adding, const QByteArray &arg = {});

  static bool rename(const QSharedPointer<QEventChannelRename> &event);

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
  void setAccountOwner(const QSharedPointer<Account> &owner);

  Flags<irc::ChannelModes> channel_modes;

  void addBan(const QByteArray &mask);
  void removeBan(const QByteArray &mask);
  QList<QByteArray> banList() const;
  int limit() const { return m_limit; }

  QByteArray uid;
  QByteArray uid_str;
  QDateTime date_creation;

signals:
  void topicChanged(const QByteArray &newTopic);
  void keyChanged(const QByteArray &newKey);

  void memberJoined(const QSharedPointer<Account> &account);
  void memberJoinedFailed(const QSharedPointer<Account> &account);
  void memberRemoved(const QSharedPointer<Account> &account);

private:
  mutable QReadWriteLock mtx_lock;
  QByteArray m_name;
  QByteArray m_topic;
  QByteArray m_key;
  QSharedPointer<Server> m_server;
  QSharedPointer<Account> m_owner;
  QList<QSharedPointer<Account>> m_members;

  // bans
  QSet<QByteArray> m_ban_masks;
  int m_limit = 0;

public:
  QVariantMap to_variantmap() {
    QReadLocker locker(&mtx_lock);
    QVariantMap obj;

    obj["uid"] = QString::fromUtf8(uid);
    obj["name"] = QString::fromUtf8(m_name);
    obj["topic"] = QString::fromUtf8(m_topic);
    obj["key"] = QString::fromUtf8(m_key);
    obj["owner"] = m_owner.isNull() ? QVariant() : QString::fromUtf8(m_owner->uid());
    obj["limit"] = m_limit;
    obj["date_creation"] = date_creation.toString(Qt::ISODate);

    // members: list of uid's
    QVariantList membersArray;
    for (const auto &member : m_members) {
      if (member) {
        membersArray.append(QString::fromUtf8(member->uid()));
      }
    }
    obj["members"] = membersArray;

    // ban masks
    QVariantList bansArray;
    for (const auto &mask : m_ban_masks) {
      bansArray.append(QString::fromUtf8(mask));
    }
    obj["ban_masks"] = bansArray;

    // channel modes as a string of letters (e.g., "+imn")
    QString modeLetters;
    for (auto it = irc::channelModesLookup.begin(); it != irc::channelModesLookup.end(); ++it) {
      if (channel_modes.has(it.key())) {
        modeLetters += it.value().letter;
      }
    }
    obj["modes"] = modeLetters;

    return obj;
  }

  rapidjson::Value to_rapidjson(rapidjson::Document::AllocatorType& allocator) {
    QReadLocker locker(&mtx_lock);
    rapidjson::Value obj(rapidjson::kObjectType);

    // strings
    obj.AddMember("uid", rapidjson::Value(uid_str.constData(), allocator), allocator);
    obj.AddMember("name", rapidjson::Value(m_name.constData(), allocator), allocator);
    obj.AddMember("topic", rapidjson::Value(m_topic.constData(), allocator), allocator);
    obj.AddMember("key", rapidjson::Value(m_key.constData(), allocator), allocator);
    obj.AddMember("owner", m_owner.isNull() ?
      rapidjson::Value() :
      rapidjson::Value(m_owner->uid_str().constData(), allocator), allocator);

    // integers
    obj.AddMember("limit", m_limit, allocator);

    // date as string
    obj.AddMember("date_creation", rapidjson::Value(date_creation.toString(Qt::ISODate).toUtf8().constData(), allocator), allocator);

    // members array
    rapidjson::Value membersArray(rapidjson::kArrayType);
    for (const auto &member : m_members) {
      if (member) {
        membersArray.PushBack(rapidjson::Value(member->uid_str().constData(), allocator), allocator);
      }
    }
    obj.AddMember("members", membersArray, allocator);

    // ban masks array
    rapidjson::Value bansArray(rapidjson::kArrayType);
    for (const auto &mask : m_ban_masks) {
      bansArray.PushBack(rapidjson::Value(mask, allocator), allocator);
    }
    obj.AddMember("ban_masks", bansArray, allocator);

    // channel modes as string
    QString modeLetters;
    for (auto it = irc::channelModesLookup.begin(); it != irc::channelModesLookup.end(); ++it) {
      if (channel_modes.has(it.key())) {
        modeLetters += it.value().letter;
      }
    }
    obj.AddMember("modes", rapidjson::Value(modeLetters.toUtf8().constData(), allocator), allocator);

    return obj;
  }
};
