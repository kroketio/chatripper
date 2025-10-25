#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QTimer>
#include <QSet>
#include <QPointer>
#include <QElapsedTimer>
#include <QReadWriteLock>
#include <QDateTime>
#include <QHash>
#include <QUuid>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "lib/globals.h"
#include "core/qtypes.h"
#include "core/metadata.h"
#include "irc/client_connection.h"

class Channel;

class Account final : public QObject {
Q_OBJECT
Q_PROPERTY(QByteArray name READ name WRITE setName NOTIFY nickChanged)
Q_PROPERTY(QUuid uid READ uid WRITE setUID)
Q_PROPERTY(QByteArray password READ password WRITE setPassword)
Q_PROPERTY(QByteArray nick READ nick WRITE setNick NOTIFY nickChanged)
Q_PROPERTY(QByteArray host READ host WRITE setHost)
Q_PROPERTY(QDateTime creation_date MEMBER creation_date)

public:
  explicit Account(const QByteArray& account_name = "", QObject* parent = nullptr);
  static QSharedPointer<Account> create_from_db(const QUuid &id, const QByteArray &username, const QByteArray &password, const QDateTime &creation);
  static QSharedPointer<Account> create();

  QSharedPointer<QEventAuthUser> verifyPassword(const QSharedPointer<QEventAuthUser> &auth) const;

  static QSharedPointer<Account> get_by_uid(const QUuid &uid);
  static QSharedPointer<Account> get_by_name(const QByteArray &name);
  // static QSharedPointer<Account> get_or_create(const QByteArray &account_name);

  void merge(const QSharedPointer<Account> &from);

  void message(QSharedPointer<QEventMessage> &message);

  void setRandomUID();

  QByteArray name();
  void setName(const QByteArray &name);

  QUuid uid() const;
  QByteArray uid_str() { return m_uid_str; }
  void setUID(const QUuid &uid);

  QByteArray password();
  void setPassword(const QByteArray &password);

  QByteArray nick();

  // exclusively used by the Python bindings
  bool setNick(const QByteArray &nick) {
    const auto acc = get_by_uid(this->uid());
    const auto event = QSharedPointer<QEventNickChange>(new QEventNickChange());
    event->setAccount(acc);
    event->new_nick = nick;
    QReadLocker rlock(&mtx_lock);
    event->old_nick = m_nick;
    rlock.unlock();
    this->setNick(event);
    return true;
  }

  bool setNickByForce(const QByteArray &nick) {
    QWriteLocker locker(&mtx_lock);
    m_nick = nick;
    return true;
  }

  bool setNick(const QSharedPointer<QEventNickChange> &event, bool broadcast = true);

  [[nodiscard]] QByteArray host() const {
    if (m_host.isEmpty())
      return g::defaultHost;
    return m_host;
  }
  void setHost(const QByteArray &host);

  [[nodiscard]] QByteArray prefix(const QByteArray &nick_override = "") const {
    QReadLocker locker(&mtx_lock);
    auto _nick = nick_override.isEmpty() ? m_nick : nick_override;
    return _nick + "!" + (m_name.isEmpty() ? "user" : m_name) + "@" + m_host;
  }

  [[nodiscard]] bool login(const QString& username, const QString& password) { return true; }
  [[nodiscard]] bool is_logged_in() { return !m_name.isEmpty(); }

  void broadcast_nick_changed(const QByteArray& msg) const;

  void add_channel(const QByteArray &channel);
  void add_connection(irc::client_connection *ptr);
  bool hasConnections() const {
    return connections.size() > 0;
  }
  void onConnectionDisconnected(irc::client_connection *conn, const QByteArray &nick_to_delete);
  void clearConnections();
  ~Account() override;

  QDateTime creation_date;

  QList<irc::client_connection*> connections;
  QHash<QByteArray, QSharedPointer<Channel>> channels;

  mutable QReadWriteLock mtx_lock;

  QSharedPointer<Metadata> metadata() {
    if (m_metadata.isNull())
      m_metadata = QSharedPointer<Metadata>::create(this);
    return m_metadata;
  }


signals:
  void nickChanged(const QByteArray& old_nick, const QByteArray& new_nick);
private:
  QUuid m_uid;
  QByteArray m_uid_str;
  QByteArray m_name;
  QByteArray m_nick;
  QByteArray m_password;
  QByteArray m_host;

  QSharedPointer<Metadata> m_metadata;

public:
  QVariantMap to_variantmap() const;
  rapidjson::Value to_rapidjson(rapidjson::Document::AllocatorType& allocator, bool include_channels = false, bool include_connection_count = false) const;
};
