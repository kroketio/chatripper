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

#include "globals.h"
#include "core/qtypes.h"
#include "irc/client_connection.h"

class Channel;

class Account : public QObject {
Q_OBJECT

public:
  explicit Account(const QByteArray& account_name = "", QObject* parent = nullptr);
  static QSharedPointer<Account> create_from_db(const QByteArray &id, const QByteArray &username, const QByteArray &password, const QDateTime &creation);

  QAuthUserResult verifyPassword(const QByteArray &password_candidate, const QHostAddress& ip) const;

  static QSharedPointer<Account> get_by_uid(const QByteArray &uid);
  static QSharedPointer<Account> get_by_name(const QByteArray &name);
  // static QSharedPointer<Account> get_or_create(const QByteArray &account_name);

  void merge(const QSharedPointer<Account> &from);

  void message(const irc::client_connection *conn, const QSharedPointer<Account> &dest, const QByteArray &message);

  void setRandomUID();

  QByteArray name();
  void setName(const QByteArray &name);

  QByteArray uid();
  void setUID(const QByteArray &uid);

  QByteArray password();
  void setPassword(const QByteArray &password);

  QByteArray nick();
  bool setNick(const QByteArray &nick);

  [[nodiscard]] QByteArray host() const {
    if (m_host.isEmpty())
      return g::defaultHost;
    return m_host;
  }
  void setHost(const QByteArray &host);

  [[nodiscard]] QByteArray prefix(const int conn_id = -1) {
    if (connections.size() > 0 && conn_id != -1) {
      const auto conn = connections.at(conn_id);
      return conn->prefix();
    }

    throw std::runtime_error("No prefix found, DEBUG ME");
  }

  [[nodiscard]] bool login(const QString& username, const QString& password) { return true; }
  [[nodiscard]] bool is_logged_in() { return !m_name.isEmpty(); }

  static void channel_join(QSharedPointer<Account> &acc, const QByteArray& channel_name);
  static void channel_part(QSharedPointer<Account> &acc, const QByteArray& channel_name, const QByteArray& message);
  void broadcast_nick_changed(const QByteArray& msg) const;

  void add_channel(const QByteArray &channel);
  void add_connection(irc::client_connection *ptr);
  ~Account() override;

  QDateTime creation_date;

  QList<irc::client_connection*> connections;
  QHash<QByteArray, QSharedPointer<Channel>> channels;

signals:
  void nickChanged(const QByteArray& old_nick, const QByteArray& new_nick);

private:
  mutable QReadWriteLock mtx_lock;

  QByteArray m_uid;
  QByteArray m_name;
  QByteArray m_nick;
  QByteArray m_password;
  QByteArray m_host;

public:
  QVariantMap to_variantmap() const;
};
