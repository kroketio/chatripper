#include <QTcpSocket>
#include <QMetaObject>
#include <QDebug>
#include <QHostInfo>
#include <QMutexLocker>

#if defined(Q_OS_UNIX) || defined(Q_OS_LINUX)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "unistd.h"
#elif defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "threaded_server.h"
#include "ctx.h"

#include "client_connection.h"

namespace irc {
  ThreadedServer::ThreadedServer(
    const int thread_count,
    const int max_per_ip,
    QObject *parent) : QTcpServer(parent),
        m_thread_count(static_cast<short>(thread_count)),
        m_max_per_ip(max_per_ip),
        m_next_worker(0) {
    if (thread_count == 0)
      throw std::runtime_error("thread count cannot be 0");

    // @TODO: replace with enums
    capabilities << "soju.im/FILEHOST";
    capabilities << "draft/metadata";
    capabilities << "draft/metadata-2";
    capabilities << "message-tags";
    capabilities << "multi-prefix";
    capabilities << "extended-join";
    capabilities << "chghost";
    capabilities << "account-tag";
    capabilities << "account-notify";
    capabilities << "echo-message";
    capabilities << "znc.in/self-message";
    // capabilities << "fish";
    capabilities << "sasl";
    capabilities << "draft/channel-rename";
    capabilities << "extended-isupport";

    // @TODO: replace with actual values
    isupport.insert("AWAYLEN", "390");
    isupport.insert("BOT", "B");
    isupport.insert("CASEMAPPING", "ascii");
    isupport.insert("CHANLIMIT", "#:100");
    isupport.insert("CHANMODES", "Ibe,k,fl,CEMRUimnstu");
    isupport.insert("CHANNELLEN", "64");
    isupport.insert("CHANTYPES", "#");
//    isupport.insert("CHATHISTORY", "1000");
//    isupport.insert("draft/CHATHISTORY", "1000");
    isupport.insert("ELIST", "U");
    isupport.insert("EXCEPTS", QByteArray());
    isupport.insert("EXTBAN", ",m");
 //   isupport.insert("EXTJWT", "1");
    isupport.insert("FORWARD", "f");

    isupport.insert("INVEX", QByteArray());
    isupport.insert("KICKLEN", "390");
    isupport.insert("MAXLIST", "beI:60");
    isupport.insert("MAXTARGETS", "4");
    isupport.insert("MODES", QByteArray());
    isupport.insert("MONITOR", "100");
    isupport.insert("MSGREFTYPES", "msgid,timestamp");
    isupport.insert("NETWORK", network_name);
    isupport.insert("NICKLEN", "32");
    isupport.insert("PREFIX", "(qaohv)~&@%+");
    isupport.insert("RPCHAN", "E");
    isupport.insert("RPUSER", "E");
    isupport.insert("SAFELIST", QByteArray());

    isupport.insert("SAFERATE", QByteArray());
    isupport.insert("STATUSMSG", "~&@%+");
    isupport.insert("TARGMAX", "NAMES:1,LIST:1,KICK:,WHOIS:1,USERHOST:10,PRIVMSG:4,TAGMSG:4,NOTICE:4,MONITOR:100");
    isupport.insert("TOPICLEN", "390");
    isupport.insert("UTF8MAPPING", "rfc8265");
    isupport.insert("UTF8ONLY", QByteArray());
//    isupport.insert("VAPID", "");
    isupport.insert("WHOX", QByteArray());

    setup_pool(thread_count);
  }

  void ThreadedServer::setup_pool(const int thread_count) {
    for (int i = 0; i < thread_count; ++i) {
      const auto thread = new QThread;
      thread->setObjectName(QString("irc_thread-%1").arg(QString::number(i+1)));

      const auto worker = new Worker(activeConnections, activeConnectionsMutex);
      worker->moveToThread(thread);

      thread->start();

      m_thread_pool.append(thread);
      m_workers.append(worker);
    }
  }

  void ThreadedServer::incomingConnection(qintptr socketDescriptor) {
    uint32_t remote_ip = 0;  // @TODO: rip ipv6
    quint16 local_port = 0;

    // max connections per IP
#if defined(Q_OS_UNIX) || defined(Q_OS_LINUX)
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int fd = static_cast<int>(socketDescriptor);

    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
      remote_ip = ntohl(addr.sin_addr.s_addr);
    }

    // get local port
    sockaddr_in local_addr{};
    socklen_t local_len = sizeof(local_addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&local_addr), &local_len) == 0)
      local_port = ntohs(local_addr.sin_port);

#elif defined(Q_OS_WIN)
    sockaddr_in addr{};
    int len = sizeof(addr);
    SOCKET fd = static_cast<SOCKET>(socketDescriptor);

    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
      remote_ip = QHostAddress(ntohl(addr.sin_addr.s_addr));
    }
#endif
    if (remote_ip != 0) {
      QMutexLocker locker(&activeConnectionsMutex);

      if (activeConnections[remote_ip] >= m_max_per_ip) {
        ::close(static_cast<int>(socketDescriptor));
#ifndef QT_NO_DEBUG_OUTPUT
        qDebug() << "rejected connection (max IPs) from" << remote_ip;
#endif

        if (g::ctx->snakepit->hasEventHandler(QEnums::QIRCEvent::PEER_MAX_CONNECTIONS)) {
          auto ev = QSharedPointer<QEventPeerMaxConnections>(new QEventPeerMaxConnections());
          ev->connections = m_max_per_ip;
          ev->ip = QHostAddress(remote_ip).toString();

          const auto result = g::ctx->snakepit->event(
            QEnums::QIRCEvent::PEER_MAX_CONNECTIONS,
            ev);
        }

        return;
      }
      activeConnections[remote_ip]++;
    }

    // round-robin dispatch
    auto* worker = m_workers[m_next_worker];
    m_next_worker = (m_next_worker + 1) % m_thread_count;

    // assign connection to worker thread
    QMetaObject::invokeMethod(
      worker, "handleConnection", Qt::QueuedConnection,
      Q_ARG(qintptr, socketDescriptor),
      Q_ARG(uint32_t, remote_ip),
      Q_ARG(quint16, local_port));
  }

  QByteArray ThreadedServer::serverName() {
    return QHostInfo::localHostName().toUtf8();
  }

  QByteArray ThreadedServer::motd() {
    reloadMotd();
    return g::irc_motd;
  }

  void ThreadedServer::reloadMotd() {
    g::irc_motd_path.refresh();
    const QFileInfo &fileInfo = g::irc_motd_path;

    if (!fileInfo.exists()) {
      QWriteLocker wlock(&mtx_lock);
      g::irc_motd = "Welcome!";
      g::irc_motd_last_modified = 0;
      return;
    }

    const time_t lastModified = fileInfo.lastModified().toSecsSinceEpoch();
    if (lastModified == g::irc_motd_last_modified) {
      // no change, keep cached
      return;
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
      QWriteLocker wlock(&mtx_lock);
      qWarning() << "Failed to open MOTD file:" << fileInfo.absoluteFilePath();
      g::irc_motd = "Welcome!";
      g::irc_motd_last_modified = 0;
      return;
    }

    const QByteArray motdData = file.readAll();
    file.close();

    QWriteLocker wlock(&mtx_lock);
    g::irc_motd = motdData;
    g::irc_motd_last_modified = lastModified;
  }

  unsigned int ThreadedServer::concurrent_peers() {
    unsigned int peers = 0;
    for (const auto&worker: m_workers) {
      QReadLocker rlock(&worker->mtx_lock);
      peers += worker->connections.size();
      rlock.unlock();
    }
    return peers;
  }

  ThreadedServer::~ThreadedServer() {
    for (QThread* thread: m_thread_pool) {
      thread->quit();
      thread->wait();
      delete thread;
    }
  }
}
