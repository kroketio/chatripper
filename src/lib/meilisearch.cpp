#include "meilisearch.h"
#include <QDebug>

Meilisearch::Meilisearch(const QString& h, const QString& i, QObject* p) : QObject(p), host(h), index(i), pendingRequests(0) {
  nam = new QNetworkAccessManager(this);
  connect(nam, &QNetworkAccessManager::finished, this, &Meilisearch::handleReply);

  onlineTimer = new QTimer(this);
  onlineTimer->setInterval(10000);
  auto fun_checkOnline = [this] {
    checkOnline([this](bool status) {
      if (status != online) {
        online = status;
        qWarning() << "onlineNess changed" << status;
        emit onlinenessChanged(status);
      }
    });
  };
  fun_checkOnline();
  connect(onlineTimer, &QTimer::timeout, this, fun_checkOnline);
  onlineTimer->start();
}

QString SearchResult::toString() const {
  return QString("<SearchResult id=%1 date=%2 message=%3...>")
    .arg(id)
    .arg(QDateTime::fromSecsSinceEpoch(date).toString("yyyy-MM-dd hh:mm:ss"))
    .arg(message.left(30));
}

void Meilisearch::setupIndex() {
  pendingRequests = 3;
  auto sendPut = [this](const QString &path, const QJsonArray &arr) {
    QNetworkRequest r(QUrl(host + "/indexes/" + index + path));
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    nam->put(r, QJsonDocument(arr).toJson());
  };

  sendPut("/settings/sortable-attributes", QJsonArray{"date"});
  sendPut("/settings/searchable-attributes", QJsonArray{"message"});
  sendPut("/settings/ranking-rules", QJsonArray{"sort", "typo", "words", "proximity", "attribute", "exactness"});
}

void Meilisearch::searchMessages(const QString &msg, int limit, int offset, const SearchCallback &callback) {
  QString requestId = QUuid::createUuid().toString();
  if (callback)
    pendingSearches[requestId] = callback;

  QJsonObject body;
  body["q"] = msg;
  body["limit"] = limit;
  body["offset"] = offset;
  body["sort"] = QJsonArray{"date:desc"};

  QNetworkRequest r(QUrl(host + "/indexes/" + index + "/search"));
  r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QNetworkReply *reply = nam->post(r, QJsonDocument(body).toJson());
  reply->setProperty("requestId", requestId);

  QTimer* timer = new QTimer(reply);
  timer->setSingleShot(true);
  timer->start(2000);

  connect(timer, &QTimer::timeout, reply, [this, reply, requestId]() {
    reply->abort();
    if (pendingSearches.contains(requestId)) {
      pendingSearches[requestId](QList<SearchResult>());
      pendingSearches.remove(requestId);
    }
  });
}

void Meilisearch::clearDb() {
  QNetworkRequest r(QUrl(host + "/indexes/" + index + "/documents"));
  nam->deleteResource(r);
}

void Meilisearch::insertMessages(const QStringList &messages) {
  QJsonArray arr;
  for (const QString &msg: messages) {
    QJsonObject o;
    o["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    o["message"] = msg;
    o["date"] = QDateTime::currentSecsSinceEpoch();
    o["remote_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    arr.append(o);
  }
  QNetworkRequest r(QUrl(host + "/indexes/" + index + "/documents"));
  r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  nam->post(r, QJsonDocument(arr).toJson());
}

void Meilisearch::handleReply(QNetworkReply *reply) {
  const QString requestId = reply->property("requestId").toString();
  const QByteArray data = reply->readAll();
  reply->deleteLater();

  const QUrl u = reply->request().url();
  if (u.path().endsWith("/search")) {
    QList<SearchResult> results;
    QJsonObject root = QJsonDocument::fromJson(data).object();
    QJsonArray hits = root["hits"].toArray();

    for (auto h: hits) {
      QJsonObject o = h.toObject();
      SearchResult s;
      s.id = o["id"].toString();
      s.message = o["message"].toString();
      s.date = o["date"].toInteger();
      s.remoteId = o["remote_id"].toString();
      results.append(s);
    }

    if (pendingSearches.contains(requestId)) {
      pendingSearches[requestId](results);
      pendingSearches.remove(requestId);
    }
  }

  if (pendingRequests > 0)
    pendingRequests--;
  if (pendingRequests == 0)
    emit setupCompleted();

  emit requestFinished();
}

void Meilisearch::checkOnline(const OnlineCallback& callback) {
  const QNetworkRequest r(QUrl(host + "/health"));
  QNetworkReply* reply = nam->get(r);

  QTimer* timer = new QTimer(reply); // parented to reply for auto deletion
  timer->setSingleShot(true);
  timer->start(2000);

  connect(timer, &QTimer::timeout, reply, [reply, callback]() {
    reply->abort();
    if(callback) callback(false);
  });

  connect(reply, &QNetworkReply::finished, this, [reply, timer, callback]() {
    timer->stop();
    const bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    if(callback) callback(ok);
  });
}
