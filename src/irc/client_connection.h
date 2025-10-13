#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QTimer>
#include <QSet>
#include <QPointer>
#include <QMutexLocker>
#include <QWriteLocker>
#include <QReadLocker>
#include <QElapsedTimer>
#include <QHash>

#include "lib/bitflags.h"
#include "irc/caps.h"
#include "irc/modes.h"
#include "core/qtypes.h"

class Channel;
class Account;

namespace irc {
  class ThreadedServer;

  class client_connection final : public QObject {
    Q_OBJECT

  public:
    enum class ConnectionSetupTasks : int {
      CAP_EXCHANGE = 1 << 0,
      NICK         = 1 << 1,
      USER         = 1 << 2
    };

    explicit client_connection(ThreadedServer* server, QTcpSocket* socket, QObject* parent = nullptr);
    ~client_connection() override;

    void handleConnection(const QHostAddress &peer_ip);

    Flags<ConnectionSetupTasks> setup_tasks;
    Flags<PROTOCOL_CAPABILITY> capabilities;
    Flags<UserModes> user_modes;

    [[nodiscard]] bool is_bot() const {
      return user_modes.has(UserModes::BEEP_BOOP_BOT);
    }

    QTcpSocket *m_socket = nullptr;
    bool logged_in = false;

    // disconnect slow setup/register
    [[nodiscard]] time_t time_connection_established() const {
      QReadLocker rlock(&mtx_lock);
      return m_time_connection_established;
    }

    // disconnect slow activity
    [[nodiscard]] time_t time_last_activity() const {
      QReadLocker rlock(&mtx_lock);
      return m_last_activity;
    }

    QByteArray user;
    QByteArray realname;

    QByteArray nick();
    bool setNick(const QByteArray &new_nick) {
      if (!m_nick.isEmpty())
        throw std::runtime_error("use account->nick()");

      QWriteLocker wlock(&mtx_lock);
      m_nick = new_nick;
      return true;
    }

    QMap<QByteArray, QSharedPointer<Channel>> channels;
    QMap<QSharedPointer<Channel>, QSet<QSharedPointer<Account>>> channel_members;

    void channel_join(const QSharedPointer<QEventChannelJoin> &event);
    void channel_send_topic(const QByteArray &channel_name, const QByteArray &topic);

    // QByteArray nickname() const { return nick; }
    // QByteArray username() const { return user; }
    QByteArray host() const { return m_host; }

    void send_raw(const QByteArray &line) const;
    void reply_num(int code, const QByteArray &text);
    void reply_self(const QByteArray &command, const QByteArray &args);

    void channel_part(const QSharedPointer<QEventChannelPart> &event);
    bool change_nick(const QSharedPointer<QEventNickChange> &event);

    void self_message(const QByteArray& target, const QSharedPointer<QEventMessage> &message);
    void message(const QSharedPointer<Account> &src, const QByteArray& target, const QSharedPointer<QEventMessage> &message) const;

    void change_host(const QSharedPointer<Account> &acc, const QByteArray &new_host);
    void change_host(const QByteArray &new_host);

    void applyUserMode(UserModes mode, bool adding);

    static QByteArray irc_lower(const QByteArray &s);
    static QList<QByteArray> split_irc(const QByteArray &line);

    void disconnect() const;
    QByteArray prefix();

  signals:
    void sendData(const QByteArray &data) const;
    void disconnected(const QByteArray &nick_to_delete);

  private slots:
    void onReadyRead();
    void onSocketDisconnected();
  public slots:
    void onWrite(const QByteArray &data) const;
  private:
    mutable QReadWriteLock mtx_lock;
    QTimer* m_inactivityTimer = nullptr;

    void parseIncoming(QByteArray &line);
    void handlePASS(const QList<QByteArray> &args);
    void handleNICK(const QList<QByteArray> &args);
    void handleUSER(const QList<QByteArray> &args);
    void handlePING(const QList<QByteArray> &args);
    void handlePONG(const QList<QByteArray> &args);
    void handleJOIN(const QList<QByteArray> &args);
    void handlePART(const QList<QByteArray> &args);
    void handlePRIVMSG(const QList<QByteArray> &args);
    void handleQUIT(const QList<QByteArray> &args);
    void handleNAMES(const QList<QByteArray> &args);
    void handleTOPIC(const QList<QByteArray> &args);
    void handleLUSERS(const QList<QByteArray> &args);
    void handleMODE(const QList<QByteArray> &args);
    void handleCAP(const QList<QByteArray> &args);
    void handleMOTD(const QList<QByteArray> &args);
    void handleAUTHENTICATE(const QList<QByteArray> &args);
    void handleWHOIS(const QList<QByteArray> &args);
    void handleWHO(const QList<QByteArray> &args);
    void try_finalize_setup();

    ThreadedServer *m_server;
    QSharedPointer<Account> m_account;

    QHostAddress m_remote;

    bool is_ready = false;

    // only used during connection setup
    bool user_already_exists = false;

    QByteArray m_nick;
    QByteArray m_buffer;
    QByteArray m_passGiven;
    QByteArray m_host;

    time_t m_last_activity = 0;
    time_t m_time_connection_established = 0;

    unsigned int m_available_modes_count;
  };
}