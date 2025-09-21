#include "module.h"

QSharedPointer<ModuleClass> ModuleClass::fromJson(const QJsonObject &obj) {
  auto mod = QSharedPointer<ModuleClass>::create();

  mod->name = obj.value("name").toString();
  mod->author = obj.value("author").toString();
  mod->version = obj.value("version").toDouble();
  mod->enabled = obj.value("enabled").toInt() != 0;
  mod->type = obj.value("type").toString();

  QJsonArray arr = obj.value("handlers").toArray();
  for (const QJsonValue &val : arr) {
    QJsonObject hObj = val.toObject();
    ModuleHandler h;
    h.event = hObj.value("event").toString();
    h.method = hObj.value("method").toString();
    mod->handlers.append(h);
  }

  return mod;
}
