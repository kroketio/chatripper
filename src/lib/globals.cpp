#include "lib/globals.h"
#include "ctx.h"

namespace g {
  QString configRoot;
  QString homeDir;
  QString configDirectory;
  QString pythonModulesDirectory;
  QString uploadsDirectory;
  QString cacheDirectory;
  QByteArray defaultHost;
  QFileInfo pathDatabase;
  QFileInfo pathDatabasePreload;
  WebSessionStore* webSessions = nullptr;
  QThread* mainThread = nullptr;
  Ctx* ctx = nullptr;
}
