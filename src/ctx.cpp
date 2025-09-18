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
    g::pythonModulesDirectory
  }));

  createDefaultFiles();

  g::defaultHost = "kroket.io";

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

  SQL::account_get_all();  // just to trigger cache insertion
  CLOCK_MEASURE_END(start_init_db_preload, "initial db load");

  // python
  diamondDogs = new DiamondDogs(this);
}

void onChannelMemberJoined(const QSharedPointer<Account> &account) {
  // g::ctx->accounts.value(account->name);
}

void onChannelMemberJoinedFailed(const QSharedPointer<Account> &account) {

}

void Ctx::createConfigDirectory(const QStringList &lst) {
  for(const auto &d: lst) {
    if(!std::filesystem::exists(d.toStdString())) {
      qDebug() << QString("Creating directory: %1").arg(d);
      if(!QDir().mkpath(d))
        throw std::runtime_error("Could not create directory " + d.toStdString());
    }
  }
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
  irc_server = new irc::Server(this);
  if (!irc_server->start(port, password, "")) {
    qCritical("Failed to start server on port %hu", port);
  }
}

void Ctx::onApplicationLog(const QString &msg) {}

Ctx::~Ctx() {}