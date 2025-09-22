#pragma once
#include <QObject>

struct QMessage {
  QByteArray id;
  QMap<QString, QVariant> tags;
  QByteArray nick;
  QByteArray user;
  QByteArray host;
  QStringList targets;
  QVariant account;
  QByteArray text;
  QByteArray raw;
  bool from_server = false;
};

struct QAuthUserResult {
  bool result = false;
  QByteArray reason;
};

enum class QModuleType : int {
  MODULE =      1 << 0,
  BOT         = 1 << 1
};

enum class QIRCEvent : int {
  AUTH_SASL_PLAIN =     1 << 0,
  CHANNEL_MSG         = 1 << 1,
  PRIVATE_MSG         = 1 << 2,
  JOIN                = 1 << 3,
  LEAVE               = 1 << 4,
};

struct ModuleHandler {
  QIRCEvent event;
  QString method;
};
