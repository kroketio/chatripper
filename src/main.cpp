#include <csignal>
#include <Python.h>

#include "irc/modes.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDirIterator>

#include "python/interpreter.h"
#include "lib/logger_std/logger_std.h"
#include "lib/globals.h"
#include "core/qtypes.h"
#include "python/type_registry.h"
#include "ctx.h"

int main(int argc, char *argv[]) {
  qRegisterMetaType<QSharedPointer<QEventBase>>("QSharedPointer<QEventBase>");
  qRegisterMetaType<QSharedPointer<QEventChannelJoin>>("QSharedPointer<QEventChannelJoin>");
  qRegisterMetaType<QSharedPointer<QEventMessage>>("QSharedPointer<QEventMessage>");
  qRegisterMetaType<QSharedPointer<QEventAuthUser>>("QSharedPointer<QEventAuthUser>");
  qRegisterMetaType<QSharedPointer<Account>>("QSharedPointer<Account>");
  qRegisterMetaType<QSharedPointer<Channel>>("QSharedPointer<Channel>");
  PyTypeRegistry::registerAll();

  auto byebye = [](int){ QCoreApplication::quit(); };
  std::signal(SIGINT, byebye);
  std::signal(SIGTERM, byebye);

  irc::initializeUserModesLookup();
  irc::initializeChannelModesLookup();

  qInfo() << "Python" << Snake::version();
  Py_Initialize();
  PyEval_SaveThread();

  const QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("qircd");
  QCoreApplication::setApplicationVersion("0.1");

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption portOpt(QStringList() << "p" << "port", "Port (default 6667).", "port", "6667");
  QCommandLineOption passOpt(QStringList() << "P" << "password", "Server password (optional).", "password", "");
  QCommandLineOption webOpt(QStringList() << "w" << "web", "Enable the web-interface.", "port", "0");
  // pg
  QCommandLineOption pgHostOpt("pg-host", "PostgreSQL host (default 127.0.0.1).", "host", "127.0.0.1");
  QCommandLineOption pgPortOpt("pg-port", "PostgreSQL port (default 5432).", "port", "5432");
  QCommandLineOption pgUserOpt("pg-user", "PostgreSQL username.", "username", "postgres");
  QCommandLineOption pgPassOpt("pg-password", "PostgreSQL password.", "password", "");
  QCommandLineOption pgDbOpt("pg-database", "PostgreSQL database (default chatripper).", "database", "chatripper");
  // ms
  QCommandLineOption msEnableOpt("ms-enable", "Enable MeiliSearch integration.");
  QCommandLineOption msHostOpt("ms-host", "MeiliSearch host (default 127.0.0.1).", "host", "127.0.0.1");
  QCommandLineOption msPortOpt("ms-port", "MeiliSearch port (default 7700).", "port", "7700");
  QCommandLineOption msApiKeyOpt("ms-apikey", "MeiliSearch API key (optional).", "apikey", "");

  parser.addOption(portOpt);
  parser.addOption(passOpt);
  parser.addOption(webOpt);
  // pg
  parser.addOption(pgHostOpt);
  parser.addOption(pgPortOpt);
  parser.addOption(pgUserOpt);
  parser.addOption(pgPassOpt);
  parser.addOption(pgDbOpt);
  // ms
  parser.addOption(msEnableOpt);
  parser.addOption(msHostOpt);
  parser.addOption(msPortOpt);
  parser.addOption(msApiKeyOpt);

  parser.process(app);

  g::ircServerListeningPort = parser.value(portOpt).toUShort();
  g::irc_motd = parser.value(passOpt).toUtf8();
  g::wsServerListeningPort = parser.value(webOpt).toUShort();
  // pg
  g::pgHost = parser.value(pgHostOpt);
  g::pgPort = parser.value(pgPortOpt).toUShort();
  g::pgUsername = parser.value(pgUserOpt);
  g::pgPassword = parser.value(pgPassOpt);
  g::pgDatabase = parser.value(pgDbOpt);
  // ms
  g::msEnabled = parser.isSet(msEnableOpt);
  g::msHost = parser.value(msHostOpt);
  g::msPort = parser.value(msPortOpt).toUShort();
  g::msApiKey = parser.value(msApiKeyOpt);
  g::msApiKey = parser.value(msApiKeyOpt);

  globals::logger_std_init();

#ifdef DEBUG
  // list qrc:// files
  QDirIterator it(":", QDirIterator::Subdirectories);
  while (it.hasNext()) { qDebug() << it.next(); }
#endif

  const auto ctx = new Ctx();
  return app.exec();
}
