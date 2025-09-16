#pragma once

#include <QObject>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QTimer>
#include <QDebug>
#include <QQueue>
#include <QThread>
#include <QMutex>
#include <QDir>

#include "lib/globals.h"
#include "lib/sql.h"

#include "core/channel.h"
#include "core/account.h"

#include "irc/client_connection.h"
#include "irc/server.h"

class Ctx : public QObject {
Q_OBJECT

public:
  explicit Ctx();
  ~Ctx() override;
  QString configRoot;
  QString homeDir;

  irc::Server *irc_server;

  QList<QSharedPointer<Account>> accounts;
  QHash<QByteArray, QSharedPointer<Account>> accounts_lookup_name;
  QHash<QByteArray, QSharedPointer<Account>> accounts_lookup_uuid;

  // need to keep track of nicks too, as on IRC they are unique
  // they need to be lowercase
  QList<QByteArray> irc_nicks;

  QHash<QByteArray, QSharedPointer<Channel>> channels;

  void startIRC(int port, const QByteArray& password);

  static Ctx* instance() {
    return g::ctx;
  }

signals:
  void applicationLog(const QString &msg);

private slots:
  // void onChannelMemberJoined(const QSharedPointer<Account> &account);
  // void onChannelMemberJoinedFailed(const QSharedPointer<Account> &account);
  void onApplicationLog(const QString &msg);

private:
  QFileInfo m_path_db;
  static void createConfigDirectory(const QStringList &lst);
};
