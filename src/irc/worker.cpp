#include "worker.h"
#include <QDebug>
#include <QMutexLocker>

#include "ctx.h"

Worker::Worker(QHash<uint32_t,int> &activeConnections, QMutex &mutex, QObject *parent) :
    m_activeConnections(activeConnections),
    m_activeConnectionsMutex(mutex),
    QObject(parent) {}

void Worker::initWS() {
  m_wsserver = new QWebSocketServer(
    QStringLiteral("IRC WebSocket Server"),
    QWebSocketServer::NonSecureMode);

  connect(m_wsserver, &QWebSocketServer::newConnection, [=] {
    QWebSocket *socket = m_wsserver->nextPendingConnection();
    socket->setMaxAllowedIncomingMessageSize(10 * 1024);

    uint32_t ip_int = 0;
    auto const addr = socket->peerAddress();
    qDebug() << addr.protocol();
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
      ip_int = addr.toIPv4Address();
    } else {
      qFatal("ipv6 address not supported");
    }

    auto* ptr = new irc::client_connection(g::ctx->irc_ws, socket, this);
    const auto conn = QSharedPointer<irc::client_connection>(ptr);
    conn->handleWSConnection(ip_int);

    connect(ptr, &irc::client_connection::disconnected, this, [=](const QByteArray& nick) {
      QMutexLocker locker(&m_activeConnectionsMutex);
      if (--m_activeConnections[ip_int] <= 0)
        m_activeConnections.remove(ip_int);
      locker.unlock();

      connections.removeAll(ptr);
    });

    connections << conn;
  });
}

void Worker::handleConnection(const qintptr socket_descriptor, uint32_t peer_ip, quint16 port) {
  if (m_wsserver == nullptr)
    initWS();

  auto* socket = new QTcpSocket(this);
  if (!socket->setSocketDescriptor(socket_descriptor)) {
    qWarning() << "Failed to set socket descriptor!";
    socket->deleteLater();

    {
      // decrement active connections
      QMutexLocker locker(&m_activeConnectionsMutex);

      if (--m_activeConnections[peer_ip] <= 0)
        m_activeConnections.remove(peer_ip);
    }

    return;
  }

  if (port == g::wsServerListeningPort) {
    m_wsserver->handleConnection(socket);
    return;
  }

  auto* ptr = new irc::client_connection(g::ctx->irc_server, socket, this);
  const auto conn = QSharedPointer<irc::client_connection>(ptr);
  conn->handleConnection(peer_ip);

  connect(ptr, &irc::client_connection::disconnected, this, [=](const QByteArray& nick) {
    QMutexLocker locker(&m_activeConnectionsMutex);
    if (--m_activeConnections[peer_ip] <= 0)
      m_activeConnections.remove(peer_ip);
    locker.unlock();

    connections.removeAll(ptr);
  });

  connections << conn;
}
