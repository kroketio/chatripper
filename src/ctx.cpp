#include <filesystem>

#include <QObject>
#include <QDir>
#include <QStandardPaths>

#include "ctx.h"
#include "lib/logger_std/logger_std.h"
#include "lib/utils.h"

using namespace std::chrono;

Ctx::Ctx() {
  g::ctx = this;
  Utils::init();

  createConfigDirectory(QStringList({
    g::configDirectory,
    g::pythonModulesDirectory,
    g::uploadsDirectory
  }));

  createDefaultFiles();

  g::defaultHost = "kroket.io";
  g::mainThread = QCoreApplication::instance()->thread();

  const bool preload = true;
  if (preload)
    QFile::remove(g::pathDatabase.filePath());

  // database
  if(!SQL::initialize()) {
    const auto msg = QString("Cannot open db at %1").arg(g::pathDatabase.filePath());
    throw std::runtime_error(msg.toStdString());
  }

  if (preload)
    SQL::preload_from_file(g::pathDatabasePreload.filePath());

  // initial loading into memory: accounts & channels
  CLOCK_MEASURE_START(start_init_db_preload);
  auto _channels = SQL::channel_get_all();
  for (auto const& channel : _channels) {
    channels[channel->name()] = channel;
    const auto accounts_channels = SQL::channel_get_members(channel->uid);
    channel->addMembers(accounts_channels);
  }

  SQL::account_get_all();  // trigger cache insertion
  CLOCK_MEASURE_END(start_init_db_preload, "initial db load");

  // irc server - threadpool 4, max 5 connections per IP
  irc_server = new irc::ThreadedServer(4, 10, this);

  if (!irc_server->listen(QHostAddress::Any, 6667))
    qFatal("Failed to start threaded server");  // @TODO: do something

  // web server
  m_web_thread = new QThread();
  m_web_thread->setObjectName(QString("webserver"));
  web_server = new WebServer();
  web_server->setHost("0.0.0.0");
  web_server->setPort(3000);
  web_server->moveToThread(m_web_thread);

  connect(m_web_thread, &QThread::started, web_server, [this]() {
    if (!web_server->start()) {
      qWarning() << "Failed to start server";
      QCoreApplication::quit();
    } else {
      qInfo() << "Webserver started";
    }
  });

  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [this] {
    web_server->stop();
    m_web_thread->quit();
    m_web_thread->wait();
    web_server->deleteLater();
    m_web_thread->deleteLater();
  });

  m_web_thread->start();

  // Python
  snakepit = new SnakePit(this);
}

bool Ctx::account_username_exists(const QByteArray &username) const {
  QReadLocker locker(&mtx_cache);
  return accounts_lookup_name.contains(username);
}

void Ctx::account_remove_cache(const QSharedPointer<Account>& ptr) {
  if (ptr.isNull())
    return;

  QWriteLocker locker(&mtx_cache);
  accounts.remove(ptr);
  accounts_lookup_uuid.remove(ptr->uid());

  const auto name = ptr->name();
  if (!name.isEmpty())
    accounts_lookup_name.remove(ptr->name());
}

void Ctx::irc_nicks_remove_cache(const QByteArray &nick) const {
  QWriteLocker locker(&mtx_cache);
  g::ctx->irc_nicks.remove(nick);
}

void Ctx::irc_nicks_insert_cache(const QByteArray &nick, const QSharedPointer<Account>& ptr) const {
  QWriteLocker locker(&mtx_cache);
  g::ctx->irc_nicks[nick] = ptr;
}

QSharedPointer<Account> Ctx::irc_nick_get(const QByteArray &nick) const {
  QReadLocker locker(&mtx_cache);
  if(g::ctx->irc_nicks.contains(nick))
    return g::ctx->irc_nicks[nick];
  return {};
}

void Ctx::account_insert_cache(const QSharedPointer<Account>& ptr) {
  QWriteLocker locker(&mtx_cache);
  accounts << ptr;
  accounts_lookup_uuid[ptr->uid()] = ptr;

  const auto name = ptr->name();
  if (!name.isEmpty())
    accounts_lookup_name[ptr->name()] = ptr;
}

QList<QVariantMap> Ctx::getAccountsByUUIDs(const QList<QByteArray> &uuids) const {
  QList<QVariantMap> result;
  QReadLocker locker(&mtx_cache);
  for (const auto &uuid : uuids) {
    if (accounts_lookup_uuid.contains(uuid)) {
      const QSharedPointer<Account> acc = accounts_lookup_uuid[uuid];
      QVariantMap map;
      map["uuid"] = QString(acc->uid());
      map["name"] = QString(acc->name());
      result.append(map);
    }
  }
  return result;
}

QList<QVariantMap> Ctx::getChannelsByUUIDs(const QList<QByteArray> &uuids) const {
  QList<QVariantMap> result;
  QReadLocker locker(&mtx_cache);
  for (const auto &uuid : uuids) {
    if (channels.contains(uuid)) {
      const QSharedPointer<Channel> ch = channels[uuid];
      QVariantMap map;
      map["uuid"] = QString(ch->uid);
      map["name"] = QString(ch->name());
      result.append(map);
    }
  }
  return result;
}

void Ctx::createDefaultFiles() {
  // Python
  auto dest_python = g::pythonModulesDirectory;
  auto files_python = {
    ":/qircd.py"
  };

  for (const auto &fp : files_python) {
    if (!QFile::exists(fp)) {
      qWarning() << "Source file does not exist, skipping:" << fp;
      continue;
    }

    QFileInfo fileInfo(fp);
    QString to_path = QString("%1%2").arg(dest_python, fileInfo.fileName());

    if (QFile::exists(to_path)) {
      qDebug() << "File already exists, skipping:" << to_path;
      continue;
    }

    if (!QFile::copy(fp, to_path)) {
      qWarning() << "Failed to copy" << fp << "to" << to_path;
      continue;
    }

    QFile::setPermissions(to_path, QFile::ExeUser | QFile::ReadUser | QFile::WriteUser);
  }
}

void Ctx::startIRC(const int port, const QByteArray& password) {
  irc_server = new irc::ThreadedServer(4, 5);
}

void Ctx::createConfigDirectory(const QStringList &lst) {
  for(const auto &d: lst) {
    if(std::filesystem::exists(d.toStdString()))
      continue;

    qDebug() << QString("Creating directory: %1").arg(d);
    if(!QDir().mkpath(d))
      throw std::runtime_error("Could not create directory " + d.toStdString());
  }
}

void Ctx::onApplicationLog(const QString &msg) {}

QList<QSharedPointer<Channel>> Ctx::get_channels_ordered() const {
  QList<QSharedPointer<Channel>> values;
  QList<QPair<uint, QSharedPointer<Channel>>> hashedList;

  for (auto it = channels.cbegin(); it != channels.cend(); ++it)
    hashedList.append(qMakePair(qHash(it.key()), it.value()));

  std::sort(hashedList.begin(), hashedList.end(), [](const auto &a, const auto &b) {
    return a.first < b.first;
  });

  for (const auto &[_, snd] : hashedList)
    values.append(snd);

  return values;
}

// @TODO: probably slow
QList<QSharedPointer<Account>> Ctx::get_accounts_ordered() const {
  QList<QSharedPointer<Account>> list = accounts.values();

  std::sort(list.begin(), list.end(), [](const QSharedPointer<Account> &a, const QSharedPointer<Account> &b) {
    if (a.isNull() || b.isNull())
      return false;
    return a->name().compare(b->name()) < 0;
  });

  return list;
}

void Ctx::permission_insert_cache(const QSharedPointer<Permission>& ptr) {
  QWriteLocker locker(&mtx_cache);
  permissions << ptr;
  permissions_lookup_uuid[ptr->uid()] = ptr;
}

void Ctx::permission_remove_cache(const QSharedPointer<Permission>& ptr) {
  if (ptr.isNull()) return;
  QWriteLocker locker(&mtx_cache);
  permissions.remove(ptr);
  permissions_lookup_uuid.remove(ptr->uid());
}

void Ctx::server_insert_cache(const QSharedPointer<Server>& ptr) {
  QWriteLocker locker(&mtx_cache);
  servers << ptr;
  servers_lookup_uuid[ptr->uid()] = ptr;
  if (!ptr->name().isEmpty())
    servers_lookup_name[ptr->name()] = ptr;
}

void Ctx::server_remove_cache(const QSharedPointer<Server>& ptr) {
  if (ptr.isNull()) return;
  QWriteLocker locker(&mtx_cache);
  servers.remove(ptr);
  servers_lookup_uuid.remove(ptr->uid());
  if (!ptr->name().isEmpty())
    servers_lookup_name.remove(ptr->name());
}

void Ctx::role_insert_cache(const QSharedPointer<Role>& ptr) {
  QWriteLocker locker(&mtx_cache);
  roles << ptr;
  roles_lookup_uuid[ptr->uid()] = ptr;
  if (!ptr->name().isEmpty())
    roles_lookup_name[ptr->name()] = ptr;
}

void Ctx::role_remove_cache(const QSharedPointer<Role>& ptr) {
  if (ptr.isNull()) return;
  QWriteLocker locker(&mtx_cache);
  roles.remove(ptr);
  roles_lookup_uuid.remove(ptr->uid());
  if (!ptr->name().isEmpty())
    roles_lookup_name.remove(ptr->name());
}

void Ctx::upload_insert_cache(const QSharedPointer<Upload>& ptr) {
  QWriteLocker locker(&mtx_cache);
  uploads << ptr;
  uploads_lookup_uuid[ptr->uid()] = ptr;
}

void Ctx::upload_remove_cache(const QSharedPointer<Upload>& ptr) {
  if (ptr.isNull()) return;
  QWriteLocker locker(&mtx_cache);
  uploads.remove(ptr);
  uploads_lookup_uuid.remove(ptr->uid());
}

void onChannelMemberJoined(const QSharedPointer<Account> &account) {
  // g::ctx->accounts.value(account->name);
}

void onChannelMemberJoinedFailed(const QSharedPointer<Account> &account) {

}

Ctx::~Ctx() {}