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
  extern QString pythonModulesDirectory;
  extern QString uploadsDirectory;
  extern QString cacheDirectory;
  extern QByteArray defaultHost;
  extern QFileInfo pathDatabase;
  extern QFileInfo pathDatabasePreload;
  extern WebSessionStore* webSessions;
  extern Ctx* ctx;
}
