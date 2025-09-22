#include <QHostInfo>
#include <QDateTime>

#include "lib/config.h"
#include "core/channel.h"
#include "core/account.h"
#include "irc/server.h"

#include "ctx.h"
#include "irc/client_connection.h"

namespace irc {
  Server::Server(QObject *parent) : QTcpServer(parent) {
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

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setInterval(15000);
    connect(m_timeoutTimer, &QTimer::timeout, this, &Server::onDefaultIdleTimeout);
    m_timeoutTimer->start();

    const auto idleTimeoutSec = config()->get(ConfigKeys::DefaultIdleTimeout).toInt();
    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(idleTimeoutSec / 4 * 1000);  // spread out
    connect(m_pingTimer, &QTimer::timeout, this, &Server::onPingTimeout);
    m_pingTimer->start();
  }

  void Server::onPingTimeout() {
    QReadLocker locker(&g::ctx->mtx_cache);

    QVector<client_connection *> ping_connections;
    ping_connections.reserve(512);

    for (const auto &acc: g::ctx->accounts) {
      for (const auto &conn: acc->connections) {
        ping_connections.push_back(conn);
      }
    }

    locker.unlock();

    if (ping_connections.isEmpty())
      return;

    const qsizetype batchSize = (ping_connections.size() + 3) / 4;
    const qsizetype start = m_pingBatchIndex * batchSize;
    const qsizetype end   = std::min(start + batchSize, ping_connections.size());

    for (int i = start; i < end; ++i) {
      const auto *conn = ping_connections[i];
      if (!conn)
        continue;

      const QByteArray token = QByteArray::number(QDateTime::currentMSecsSinceEpoch());
      const QByteArray out = "PING :" + token + "\r\n";
      conn->m_socket->write(out);
    }

    m_pingBatchIndex = (m_pingBatchIndex + 1) % 4;
  }

  void Server::onDefaultIdleTimeout() {
    QReadLocker locker(&g::ctx->mtx_cache);
    const int defaultIdleTimeout = config()->get(ConfigKeys::DefaultIdleTimeout).toInt();

    // @TODO: make a QSet for client_connections
    QSet<client_connection *> dead_connections;
    for (const auto &acc: g::ctx->accounts) {
      for (const auto &conn: acc->connections) {
        // get the last ping time using accessor
        time_t last_ping = conn->time_last_activity();

        // if last ping is not set (0), fall back to connection established time
        if (last_ping == 0)
          last_ping = conn->time_connection_established();

        const time_t now = std::time(nullptr);
        double idle_duration = difftime(now, last_ping);
        idle_duration -= 3; // cheat a bit

        if (idle_duration > defaultIdleTimeout) {
          dead_connections << conn;
        }
      }
    }

    locker.unlock();

    // force disconnect
    for (const auto &conn: dead_connections) {
      qDebug() << "ded connection";
      conn->m_socket->disconnectFromHost();
    }
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
