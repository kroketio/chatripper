#pragma once

#include <QObject>
#include <QProcess>
#include <QFileInfo>
#include <QSettings>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTcpSocket>
#include <QTextCharFormat>
#include <QImageReader>
#include <chrono>
#include <iostream>
#include <unistd.h>

#include "lib/globals.h"

class Utils
{

public:
  static void init();
  static bool readJsonFile(QIODevice &device, QSettings::SettingsMap &map);
  static bool writeJsonFile(QIODevice &device, const QSettings::SettingsMap &map);
  static bool validateJSON(const QByteArray &blob);
  static bool validateJSON(const QString &message);
  static bool fileExists(const QString &path);
  static QByteArray fileOpen(const QString &path);
  static QByteArray fileTextOpen(const QString &path);
  static QByteArray fileOpenQRC(const QString &path);
  static bool fileWrite(const QString &path, const QString &data);
  static QList<QFileInfo> fileFind(const QRegularExpression &pattern, const QString &baseDir, int level, int depth, int maxPerDir);
  static bool isCyrillic(const QString &inp);
  static bool dirExists(const QString &path);
  static bool portOpen(const QString &hostname, quint16 port);
  static QString humanFileSize(double num_bytes);
  static QFileInfo tempFile(QString suffix);
  static std::chrono::time_point<std::chrono::high_resolution_clock> timeStart();
  static void timeEnd(std::string label, std::chrono::time_point<std::chrono::high_resolution_clock> start);

  static unsigned int getCpuCount()
  {
    int cpuCount = 1;

#if defined(Q_OS_WIN32) || defined(Q_OS_WIN64)
    {
      SYSTEM_INFO    si;
      GetSystemInfo(&si);
      cpuCount = si.dwNumberOfProcessors;
    }
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACX)
    cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(Q_OS_MACX)
    kern_return_t		kr;
    struct host_basic_info	hostinfo;
    unsigned int			count;

    count = HOST_BASIC_INFO_COUNT;
    kr = host_info(mach_host_self(), HOST_BASIC_INFO, (host_info_t)&hostinfo, &count);
    if(kr == KERN_SUCCESS) {
      cpuCount = hostinfo.avail_cpus;
    }

#endif

    if( cpuCount < 1 )
      cpuCount = 1;

    return cpuCount;
  }

};
