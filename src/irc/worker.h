#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QHash>
#include <QHostAddress>
#include <QThread>
#include <QMutex>

#include "client_connection.h"

class Worker final : public QObject {
Q_OBJECT

public:
  explicit Worker(QHash<QHostAddress,int> &activeConnections, QMutex &mutex, QObject *parent = nullptr);

public slots:
  void handleConnection(qintptr socket_descriptor, const QHostAddress& peer_ip);

private:
  QList<QSharedPointer<irc::client_connection>> connections;
  QHash<QHostAddress,int> &m_activeConnections;
  QMutex &m_activeConnectionsMutex;

  QTcpSocket *socket = nullptr;
};
