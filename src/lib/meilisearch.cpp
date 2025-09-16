#include "meilisearch.h"

MeiliProc::MeiliProc(QObject *parent)
    : QObject(parent),
    m_proc(new QProcess(this)) {

  connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
    const QByteArray output = m_proc->readAllStandardOutput();
    emit log(QString::fromUtf8(output));
  });

  connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
    const QByteArray error = m_proc->readAllStandardError();
    emit log(QString::fromUtf8(error));
  });
}

void MeiliProc::start() {
  // if(status != Aria2Status::idle) return;
  m_proc->start();

  const auto state = m_proc->state();
  if (state == QProcess::ProcessState::Running || state == QProcess::ProcessState::Starting) {
    emit log("Can't start meilisearch, already running or starting");
    return;
  }

  if (Utils::portOpen("127.0.0.", 7700)) {
    emit log(QString("Unable to start meilisearch on %1:%2. Port already in use.").arg("127.0.0.1", 7700));
    return;
  }

  qDebug() << QString("Start process: %1").arg(this->m_pathProc);
  QStringList arguments;

  arguments << "--no-analytics";
  arguments << "--db-path" << "/tmp/lel/";

  qDebug() << QString("%1 %2").arg(this->m_pathProc, arguments.join(" "));

  m_proc->start(this->m_pathProc, arguments);
}

void MeiliProc::stop() const {
  m_proc->terminate();
}

MeiliProc::~MeiliProc() = default;

QString SearchResult::toString() const {
  return QString("<SearchResult id=%1 date=%2 message=%3...>")
   .arg(id)
   .arg(QDateTime::fromSecsSinceEpoch(date).toString("yyyy-MM-dd hh:mm:ss"))
   .arg(message.left(30));
}

MessageDB::MessageDB(const QString& h, const QString& i, QObject* p)
    : QObject(p), host(h), index(i), pendingRequests(0)
{
  nam = new QNetworkAccessManager(this);
  connect(nam, &QNetworkAccessManager::finished, this, &MessageDB::handleReply);

  // check onlineness every 10 sec
  onlineTimer = new QTimer(this);
  onlineTimer->setInterval(10000);
  auto fun_checkOnline = [this] {
    checkOnline([this](const bool status) {
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

void MessageDB::setupIndex() {
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

void MessageDB::searchMessages(const QString &msg, int limit, int offset, const SearchCallback &callback) {
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

void MessageDB::clearDb() {
  QNetworkRequest r(QUrl(host + "/indexes/" + index + "/documents"));
  nam->deleteResource(r);
}

// @TODO: what happens if this fails? :P
void MessageDB::insertMessages(const QStringList &messages) {
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

void MessageDB::handleReply(QNetworkReply *reply) {
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

// db->checkOnline([](bool online){
//     if(online) qDebug() << "Meilisearch is online";
//     else qDebug() << "Meilisearch is offline";
// });
void MessageDB::checkOnline(const OnlineCallback& callback) {
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
