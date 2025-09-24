#pragma once
#include <QObject>
#include <QHttpServer>
#include <QTcpServer>
#include <QSharedPointer>

#include "ratelimiter.h"
#include "sessionstore.h"

class WebServer : public QObject {
  Q_OBJECT

public:
  explicit WebServer(QObject *parent = nullptr);
  ~WebServer() override;

  // configure host/port before start()
  void setHost(const QString &host);
  void setPort(quint16 port);

public slots:
  bool start();
  void stop();

signals:
  void started();
  void failed(const QString &reason);

private:
  QHttpServer *m_server = nullptr;
  QTcpServer *m_tcp_server = nullptr;
  QString m_host = "0.0.0.0";
  quint16 m_port = 3000;

  RateLimiter* m_loginRateLimiter = nullptr;
  RateLimiter* m_uploadRateLimiter = nullptr;

  void registerRoutes();
};
