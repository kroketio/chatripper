#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QPointer>
#include <QTimer>

#include "core/channel.h"

namespace irc {
  class client_connection;
  class channel;

  class Server : public QTcpServer {
    Q_OBJECT

  public:
    explicit Server(QObject *parent = nullptr);

    bool start(quint16 port, const QByteArray &password, const QByteArray &motd);
    static QByteArray serverName() ;

    QByteArray password() const { return m_password; }
    QByteArray motd() const { return m_motd; }

    // State
    client_connection *getClientByNick(const QByteArray &nick) const;
    void changeNick(client_connection *c, const QByteArray &oldNick);
    Channel *getOrCreateChannel(const QByteArray &name);
    void removeChannelIfEmpty(Channel *ch);

    QHash<qintptr, client_connection*> clients;
    QMap<QByteArray, QSharedPointer<Channel>> channels;

    QStringList capabilities;

  private slots:
    void onDisconnectSlowClients();
    void onDefaultIdleTimeout();
    void onPingTimeout();

  protected:
    void incomingConnection(qintptr socketDescriptor) override;

  private slots:
    void onClientDisconnected();

  private:
    QByteArray m_password;
    QByteArray m_motd;

    QTimer* m_pingTimer = nullptr;
    unsigned int m_pingBatchIndex = 0;

    QTimer* m_disconnectSlowClientsTimer = nullptr;
    QTimer* m_timeoutTimer = nullptr;

    unsigned int m_timeout_slow_clients = 3;
  };

}