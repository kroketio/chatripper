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
  QModuleType type = QModuleType::MODULE;
  QList<ModuleHandler> handlers;

  ModuleClass() = default;
  static QSharedPointer<ModuleClass> create_from_json(const QJsonObject &obj);
};

Q_DECLARE_METATYPE(QMessage)
Q_DECLARE_METATYPE(QAuthUserResult)