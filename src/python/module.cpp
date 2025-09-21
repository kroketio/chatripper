#include "module.h"

// convert string to QIRCEvent enum
static QIRCEvent parseQIRCEvent(const QString &str) {
  if (str == "AUTH_USER") return QIRCEvent::AUTH_USER;
  if (str == "CHANNEL_MSG") return QIRCEvent::CHANNEL_MSG;
  if (str == "PRIVATE_MSG") return QIRCEvent::PRIVATE_MSG;
  if (str == "JOIN") return QIRCEvent::JOIN;
  if (str == "LEAVE") return QIRCEvent::LEAVE;
  return QIRCEvent::CHANNEL_MSG;
}

// convert string to ModuleType enum
static ModuleType parseModuleType(const QString &str) {
  if (str == "MODULE") return ModuleType::MODULE;
  if (str == "BOT") return ModuleType::BOT;
  return ModuleType::MODULE;
}

// parse QJsonObject into ModuleClass
QSharedPointer<ModuleClass> ModuleClass::create_from_json(const QJsonObject &obj) {
  auto module = QSharedPointer<ModuleClass>::create();

  module->name = obj.value("name").toString();
  module->author = obj.value("author").toString();
  module->version = obj.value("version").toDouble();
  module->enabled = obj.value("enabled").toBool();
  module->type = parseModuleType(obj.value("type").toString());

  QJsonArray handlersArr = obj.value("handlers").toArray();
  for (const QJsonValue &v : handlersArr) {
    QJsonObject hObj = v.toObject();
    ModuleHandler h;
    QString evtStr = hObj.value("event").toString();

    // strip enum prefix if present: "QIRCEvent.CHANNEL_MSG" -> "CHANNEL_MSG"
    if (evtStr.contains('.')) {
      evtStr = evtStr.split('.').last();
    }

    h.event = parseQIRCEvent(evtStr);
    h.method = hObj.value("method").toString();
    module->handlers.append(h);
  }

  return module;
}
