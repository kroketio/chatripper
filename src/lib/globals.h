#pragma once
#include <QObject>
#include <QDebug>
#include <QFileInfo>

class Ctx;

namespace g {
  extern QString configRoot;
  extern QString homeDir;
  extern QString configDirectory;
  extern QString cacheDirectory;
  extern QByteArray defaultHost;
  extern QFileInfo pathDatabase;
  extern QFileInfo pathDatabasePreload;

  extern Ctx* ctx;
}
