#include "irc/server.h"
#include "irc/modes.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

#include "lib/logger_std/logger_std.h"
#include "ctx.h"

int main(int argc, char *argv[]) {
  irc::initializeUserModesLookup();

  const QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("qircd");
  QCoreApplication::setApplicationVersion("0.1");

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption portOpt(QStringList() << "p" << "port", "Port (default 6667).", "port", "6667");
  QCommandLineOption passOpt(QStringList() << "P" << "password", "Server password (optional).", "password", "");
  parser.addOption(portOpt);
  parser.addOption(passOpt);
  parser.process(app);

  const quint16 port = parser.value(portOpt).toUShort();
  const QByteArray password = parser.value(passOpt).toUtf8();

  globals::logger_std_init();

  const auto ctx = new Ctx();
  ctx->startIRC(port, password);

  qInfo("IRC server listening on port %hu", port);
  return app.exec();
}
