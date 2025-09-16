#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>
#include <QProcess>
#include <QMap>
#include <functional>

#include "lib/utils.h"

class MeiliProc : public QObject {
Q_OBJECT

public:
  explicit MeiliProc(QObject *parent = nullptr);

  ~MeiliProc() override;

  void start();
  void stop() const;

signals:
  void log(QString msg);

private:
  QString m_pathProc;
  QProcess *m_proc;
};

struct SearchResult {
  QString id;
  QString message;
  qint64 date;
  QString remoteId;
  [[nodiscard]] QString toString() const;
};

class MessageDB : public QObject {
  Q_OBJECT
public:
  using SearchCallback = std::function<void(QList<SearchResult>)>;
  explicit MessageDB(const QString& host="http://127.0.0.1:7700", const QString& index="messages", QObject* parent = nullptr);
  void setupIndex();
  void searchMessages(const QString& msg,int limit=10,int offset=0, const SearchCallback &callback=nullptr);
  void clearDb();
  using OnlineCallback = std::function<void(bool)>;
  void checkOnline(const OnlineCallback& callback);
  void insertMessages(const QStringList& messages);

  bool online = false;

signals:
  void requestFinished();
  void setupCompleted();
  void onlinenessChanged(bool online);

private slots:
  void handleReply(QNetworkReply* reply);

private:
  QString host;
  QString index;
  QNetworkAccessManager* nam;
  int pendingRequests;
  QMap<QString, SearchCallback> pendingSearches;

  QTimer* onlineTimer;
};
