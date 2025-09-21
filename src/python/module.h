#pragma once

#include <QString>
#include <QList>
#include <QSharedPointer>
#include <QJsonObject>
#include <QJsonArray>

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
  QString reason;
};

enum class ModuleType {
  MODULE,
  BOT
};

enum class QIRCEvent {
  AUTH_USER,
  CHANNEL_MSG,
  PRIVATE_MSG,
  JOIN,
  LEAVE
};

struct ModuleHandler {
  QIRCEvent event;
  QString method;
};

class ModuleClass final {
public:
  QString name;
  QString author;
  double version = 0.0;
  bool enabled = false;
  ModuleType type = ModuleType::MODULE;
  QList<ModuleHandler> handlers;

  ModuleClass() = default;
  static QSharedPointer<ModuleClass> create_from_json(const QJsonObject &obj);
};

Q_DECLARE_METATYPE(QMessage)
Q_DECLARE_METATYPE(QAuthUserResult)