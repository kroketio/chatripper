#pragma once

#include <QString>
#include <QList>
#include <QSharedPointer>
#include <QJsonObject>
#include <QJsonArray>

#include "core/qtypes.h"

class ModuleClass final {
public:
  QString name;
  QString author;
  double version = 0.0;
  bool enabled = false;
  QEnums::QModuleType type = QEnums::QModuleType::MODULE;
  QEnums::QModuleMode mode = QEnums::QModuleMode::CONCURRENT;
  QList<ModuleHandler> handlers;

  ModuleClass() = default;
  static QSharedPointer<ModuleClass> create_from_json(const QJsonObject &obj);
};
