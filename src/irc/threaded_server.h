#pragma once

#include <QTcpServer>
#include <QHash>
#include <QHostAddress>
#include <QMutex>
#include <QThread>
#include <QFileInfo>
#include <QReadWriteLock>

#include "lib/globals.h"
#include "worker.h"

namespace irc {
  class ThreadedServer final : public QTcpServer {
    Q_OBJECT

  public:
    explicit ThreadedServer(
      int thread_count,
      int max_per_ip,
      QObject *parent = nullptr);
    ~ThreadedServer() override;

    QByteArray password() const { return m_password; }
    static QByteArray serverName();
    QByteArray motd();
    QStringList capabilities;

    unsigned int concurrent_peers();

    QHash<uint32_t,int> activeConnections;
    QMutex activeConnectionsMutex;

  protected:
    void incomingConnection(qintptr socketDescriptor) override;

  private:
    void reloadMotd();
    void setup_pool(int thread_count);

    int m_max_per_ip;
    QByteArray m_password;
    QByteArray m_motd;

  private:
    mutable QReadWriteLock mtx_lock;
    QList<QThread*> m_thread_pool;
    short m_thread_count;
    QList<Worker*> m_workers;
    int m_next_worker;
  };
}