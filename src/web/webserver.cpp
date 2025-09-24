#include "webserver.h"
#include <QHttpServerResponse>
#include <QHttpServerRequest>
#include <QHostAddress>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QTemporaryFile>
#include <QtConcurrent>

#include "web/routes/authroute.h"
#include "web/routes/channelsroute.h"
#include "web/routes/uploadroute.h"

#include "lib/utils.h"

WebServer::WebServer(QObject *parent) : QObject(parent),
    m_server(new QHttpServer(this)),
    m_tcp_server(new QTcpServer(this)) {
  m_loginRateLimiter = new RateLimiter(3, 5);
  m_uploadRateLimiter = new RateLimiter(3, 5);
  if (g::webSessions == nullptr)
    g::webSessions = new WebSessionStore();
  registerRoutes();
}

void WebServer::setHost(const QString &host) { m_host = host; }
void WebServer::setPort(quint16 port) { m_port = port; }

bool WebServer::start() {
  QHostAddress addr(m_host);
  if (!m_tcp_server->listen(addr, m_port) || !m_server->bind(m_tcp_server)) {
    emit failed(tr("Server failed to listen on %1:%2").arg(m_host).arg(m_port));
    return false;
  }
  emit started();
  return true;
}

void WebServer::registerRoutes() {
  // provide helpers to routes via lambdas capturing 'this'
  using namespace std::placeholders;

  // root
  m_server->route("/", [] {
    return QStringLiteral("Hello from QHttpServer (Qt6)");
  });

  // login route (delegates to routes/authroute)
  AuthRoute::install(m_server, m_loginRateLimiter);

  // channels
  ChannelsRoute::install(m_server);

  // upload route (requires valid session)
  UploadRoute::install(m_server, m_uploadRateLimiter);
}

void WebServer::stop() {
  if (m_tcp_server->isListening())
    m_tcp_server->close();
  // QHttpServer doesn't need explicit stop; destructor will cleanup
}

WebServer::~WebServer() {
  stop();
}