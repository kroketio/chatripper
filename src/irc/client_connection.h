#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QTimer>
#include <QSet>
#include <QPointer>
#include <QElapsedTimer>
#include <QHash>

#include "lib/bitflags.h"
#include "irc/caps.h"
#include "irc/modes.h"

class Channel;
class Account;

namespace irc {
  class Server;

  class client_connection : public QObject {
    Q_OBJECT

  public:
    enum class ConnectionSetupTasks : int {
      CAP_EXCHANGE = 1 << 0,
      NICK         = 1 << 1,
      USER         = 1 << 2
    };

    client_connection(Server *server, QTcpSocket *socket);
    ~client_connection() override;

    Flags<ConnectionSetupTasks> setup_tasks;
    Flags<PROTOCOL_CAPABILITY> capabilities;
    Flags<UserModes> user_modes;

    [[nodiscard]] bool is_bot() const {
      return user_modes.has(UserModes::BEEP_BOOP_BOT);
    }

    QTcpSocket *m_socket;
    bool logged_in = false;

    // disconnect slow setup/register
    time_t time_connection_established() const { return m_time_connection_established; }
    // disconnect slow activity
    time_t time_last_activity() const { return m_last_activity; }

    QByteArray nick;
    QByteArray user;
    QByteArray realname;

    QMap<QByteArray, QSharedPointer<Channel>> channels;
    QMap<QSharedPointer<Channel>, QSet<QSharedPointer<Account>>> channel_members;

    void channel_join(const QSharedPointer<Channel> &channel, const QSharedPointer<Account> &account, const QByteArray &password);
    void channel_join(const QByteArray &channel_name, const QByteArray &password);
    void channel_send_topic(const QByteArray &channel_name, const QByteArray &topic);

    // QByteArray nickname() const { return nick; }
    // QByteArray username() const { return user; }
    QByteArray host() const { return m_host; }

    void send_raw(const QByteArray &line);
    void reply_num(int code, const QByteArray &text);
    void reply_self(const QByteArray &command, const QByteArray &args);

    void channel_part(const QSharedPointer<Account> &account, const QSharedPointer<Channel> &channel, const QByteArray &message = "");
    void channel_part(const QByteArray &channel_name, const QByteArray &message = "");

    bool change_nick(const QByteArray &new_nick);
    bool change_nick(const QSharedPointer<Account> &acc, const QByteArray &old_nick, const QByteArray &new_nick);

    void self_message(const QByteArray& target, const QByteArray &message) const;
    void message(const QSharedPointer<Account> &src, const QByteArray& target, const QByteArray &message) const;

    void change_host(const QSharedPointer<Account> &acc, const QByteArray &new_host);
    void change_host(const QByteArray &new_host);

    void applyUserMode(UserModes mode, bool adding);

    static QByteArray irc_lower(const QByteArray &s);
    static QList<QByteArray> split_irc(const QByteArray &line);

    void disconnect() const;
    QByteArray prefix() const;

  signals:
    void disconnected(const QByteArray &nick_to_delete);

  private slots:
    void onReadyRead();
    void onSocketDisconnected();

  private:
    void parseIncoming(const QByteArray &line);
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

    void broadcastToChannel(Channel *ch, const QByteArray &command, const QByteArray &argtail, bool includeSelf);

    Server *m_server;
    QSharedPointer<Account> m_account;

    bool _is_setup = false;

    // only used during connection setup
    bool user_already_exists = false;

    QByteArray m_buffer;
    QByteArray m_passGiven;
    QByteArray m_host;

    time_t m_last_activity = 0;
    time_t m_time_connection_established = 0;

    unsigned int m_available_modes_count;
  };
}