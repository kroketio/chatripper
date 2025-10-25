#include "lib/globals.h"
#include "ctx.h"

namespace g {
  QString configRoot;
  QString homeDir;
  QString configDirectory;
  QString pythonModulesDirectory;
  QString uploadsDirectory;
  QString cacheDirectory;
  QString staticDirectory;
  QByteArray defaultHost;
  QFileInfo pathDatabasePreload;
  QByteArray irc_motd;
  unsigned int irc_motd_size;
  QFileInfo irc_motd_path;
  time_t irc_motd_last_modified;
  QByteArray ircServerListeningHost;
  quint16 ircServerListeningPort;
  QByteArray wsServerListeningHost;
  quint16 wsServerListeningPort;
  WebSessionStore* webSessions = nullptr;
  QThread* mainThread = nullptr;
  Ctx* ctx = nullptr;
  // pg
  QString pgHost;
  quint16 pgPort;
  QString pgUsername;
  QString pgPassword;
  QString pgDatabase;
  // meilisearch
  bool msEnabled = false;
  QString msHost;
  quint16 msPort;
  QString msApiKey;
}
