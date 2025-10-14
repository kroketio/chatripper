#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QHash>
#include <QHostAddress>
#include <QThread>
#include <QMutex>
#include <QWebSocketServer>

#include "client_connection.h"

class Worker final : public QObject {
Q_OBJECT

public:
  explicit Worker(QHash<uint32_t,int> &activeConnections, QMutex &mutex, QObject *parent = nullptr);

public slots:
  void handleConnection(qintptr socket_descriptor, uint32_t peer_ip, quint16 port);

private:
  void initWS();
  QList<QSharedPointer<irc::client_connection>> connections;
  QHash<uint32_t,int> &m_activeConnections;
  QMutex &m_activeConnectionsMutex;

  QWebSocketServer *m_wsserver = nullptr;
  QTcpSocket *socket = nullptr;
};
