#include "worker.h"
#include <QDebug>
#include <QMutexLocker>

#include "ctx.h"

Worker::Worker(QHash<QHostAddress,int> &activeConnections, QMutex &mutex, QObject *parent) :
    m_activeConnections(activeConnections),
    m_activeConnectionsMutex(mutex),
    QObject(parent) {
  int wegw = 1;
}

void Worker::handleConnection(const qintptr socket_descriptor, const QHostAddress& peer_ip) {
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
