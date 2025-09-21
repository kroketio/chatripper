#pragma once
#include <QString>
#include <QList>
#include <QSharedPointer>
#include <QJsonObject>
#include <QJsonArray>

struct ModuleHandler {
  QString event;
  QString method;
};

class ModuleClass final {
public:
  QString name;
  QString author;
  double version = 0.0;
  bool enabled = false;
  QString type;
  QList<ModuleHandler> handlers;

  ModuleClass() = default;

  // Factory method
  static QSharedPointer<ModuleClass> fromJson(const QJsonObject &obj);
};
