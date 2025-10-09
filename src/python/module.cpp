#include "module.h"

// parse QJsonObject into ModuleClass
QSharedPointer<ModuleClass> ModuleClass::create_from_json(const QJsonObject &obj) {
  auto module = QSharedPointer<ModuleClass>::create();

  module->name = obj.value("name").toString();
  module->author = obj.value("author").toString();
  module->version = obj.value("version").toDouble();
  module->enabled = obj.value("enabled").toBool();
  module->type = static_cast<QEnums::QModuleType>(obj.value("type").toInt());
  module->mode = static_cast<QEnums::QModuleMode>(obj.value("mode").toInt());

  QJsonArray handlersArr = obj.value("handlers").toArray();
  for (const QJsonValue &v : handlersArr) {
    QJsonObject hObj = v.toObject();
    ModuleHandler h;
    int ev = hObj.value("event").toInt();
    h.event = static_cast<QEnums::QIRCEvent>(ev);

    h.method = hObj.value("method").toString();
    module->handlers.append(h);
  }

  return module;
}
