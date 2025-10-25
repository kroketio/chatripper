#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QTimer>
#include <QUuid>
#include <QMap>
#include <functional>

#include "lib/utils.h"

struct SearchResult {
  QString id;
  QString message;
  qint64 date;
  QString remoteId;
  [[nodiscard]] QString toString() const;
};

class Meilisearch final : public QObject {
Q_OBJECT
public:
  using SearchCallback = std::function<void(QList<SearchResult>)>;
  using OnlineCallback = std::function<void(bool)>;

  explicit Meilisearch(const QString& host="http://127.0.0.1:7700", const QString& index="messages", QObject* parent = nullptr);

  // network actions
  void setupIndex();
  void searchMessages(const QString& msg, int limit=10, int offset=0, const SearchCallback &callback=nullptr);
  void clearDb();
  void insertMessages(const QStringList& messages);
  void checkOnline(const OnlineCallback& callback);

  bool online = false;

signals:
  void requestFinished();
  void setupCompleted();
  void onlinenessChanged(bool online);
  void log(QString msg);

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
