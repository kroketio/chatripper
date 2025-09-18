#include <QHostInfo>
#include <QDateTime>

#include "core/channel.h"
#include "core/account.h"
#include "irc/server.h"
#include "irc/client_connection.h"

namespace irc {
  Server::Server(QObject *parent) :
    QTcpServer(parent) {
    // @TODO: enums
    capabilities << "message-tags";
    capabilities << "multi-prefix";
    capabilities << "extended-join";
    capabilities << "chghost";
    capabilities << "account-tag";
    capabilities << "account-notify";
    // capabilities << "echo-message"; // @TODO implement
    capabilities << "znc.in/self-message";
    capabilities << "fish";
    capabilities << "sasl";

    m_disconnectSlowClientsTimer = new QTimer(this);
    m_disconnectSlowClientsTimer->setInterval(1000);
    connect(m_disconnectSlowClientsTimer, &QTimer::timeout, this, &Server::onDisconnectSlowClients);
    m_disconnectSlowClientsTimer->start();
  }

  bool Server::start(const quint16 port, const QByteArray &password, const QByteArray &motd) {
    m_password = password;
    m_motd = motd;
    return listen(QHostAddress::Any, port);
  }

  void Server::onDisconnectSlowClients() {
    const auto now = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    for (const auto& conn: clients) {
      if (conn->setup_tasks.empty())
        continue;

      // if ((now - conn->time_connection_established) >= m_timeout_slow_clients) {
      //   qDebug() << "client disconnected: too slow registering";
      //   conn->disconnect();
      // }
    }
  }

  QByteArray Server::serverName() {
    return QHostInfo::localHostName().toUtf8();
  }

  void Server::incomingConnection(const qintptr socketDescriptor) {
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
      socket->deleteLater();
      return;
    }

    // @TODO: throttle / anti-spam
    
    auto *ptr = new client_connection(this, socket);
    clients.insert(socketDescriptor, ptr);
    connect(ptr, &client_connection::disconnected, this, &Server::onClientDisconnected);
  }

  void Server::onClientDisconnected() {
    auto *connection = qobject_cast<client_connection *>(sender());
    if (!connection)
      return;

    qintptr key = -1;
    for (auto it = clients.begin(); it != clients.end(); ++it) {
      if (it.value() == connection) {
        key = it.key();
        break;
      }
    }
    if (key != -1)
      clients.remove(key);
    connection->deleteLater();
  }
}
