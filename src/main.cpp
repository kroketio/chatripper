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
  parser.addOption(portOpt);
  parser.addOption(passOpt);
  parser.addOption(webOpt);
  parser.process(app);

  const quint16 port = parser.value(portOpt).toUShort();
  const QByteArray password = parser.value(passOpt).toUtf8();
  const quint16 web = parser.value(webOpt).toUShort();

  globals::logger_std_init();

#ifdef DEBUG
  // list qrc:// files
  QDirIterator it(":", QDirIterator::Subdirectories);
  while (it.hasNext()) { qDebug() << it.next(); }
#endif

  const auto ctx = new Ctx();
  ctx->startIRC(port, password);

  qInfo("IRC server listening on port %hu", port);
  return app.exec();
}
