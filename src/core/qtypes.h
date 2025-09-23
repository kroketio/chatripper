#pragma once
#include <QObject>
#include <QByteArray>
#include <QVector>
#include <QMap>

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

  QVariantMap to_variantmap() const {
    QVariantMap map;

    map["id"] = id;

    QVariantMap tagMap;
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
      tagMap[it.key()] = it.value();
    }
    map["tags"] = tagMap;

    map["nick"] = nick;
    map["user"] = user;
    map["host"] = host;

    QVariantList targetList;
    for (const auto& t : targets) {
      targetList.append(t.toUtf8());
    }
    map["targets"] = targetList;

    map["account"] = account;
    map["text"] = QString::fromUtf8(text);
    map["raw"] = raw;

    map["from_server"] = from_server;

    return map;
  }
};

struct QAuthUserResult {
  bool result = false;
  QByteArray reason;
};

enum class QModuleType : int {
  MODULE =      1 << 0,
  BOT         = 1 << 1
};

enum class QModuleMode : int {
  CONCURRENT =      1 << 0,
  EXCLUSIVE  =      1 << 1
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
