#pragma once
#include <QObject>
#include <QDebug>
#include <QFileInfo>

#include "web/sessionstore.h"

class Ctx;

namespace g {
  extern QString configRoot;
  extern QString homeDir;
  extern QString configDirectory;
  extern QFileInfo pathDatabasePreload;
  extern QString pythonModulesDirectory;
  extern QString uploadsDirectory;
  extern QString cacheDirectory;
  extern QString staticDirectory;
  extern QByteArray defaultHost;
  extern QByteArray irc_motd;
  extern QFileInfo irc_motd_path;
  extern QByteArray ircServerListeningHost;
  extern quint16 ircServerListeningPort;
  extern QByteArray wsServerListeningHost;
  extern quint16 wsServerListeningPort;
  extern unsigned int irc_motd_size;
  extern time_t irc_motd_last_modified;
  extern WebSessionStore* webSessions;
  extern QThread* mainThread;
  extern Ctx* ctx;
  // pg
  extern QString pgHost;
  extern quint16 pgPort;
  extern QString pgUsername;
  extern QString pgPassword;
  extern QString pgDatabase;
  // meilisearch
  extern bool msEnabled;
  extern QString msHost;
  extern quint16 msPort;
  extern QString msApiKey;
}
