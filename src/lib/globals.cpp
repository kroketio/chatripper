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
  QFileInfo pathDatabase;
  QFileInfo pathDatabasePreload;
  QByteArray irc_motd;
  unsigned int irc_motd_size;
  QFileInfo irc_motd_path;
  time_t irc_motd_last_modified;
  WebSessionStore* webSessions = nullptr;
  QThread* mainThread = nullptr;
  Ctx* ctx = nullptr;
}
