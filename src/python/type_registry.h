#pragma once
#include <QString>
#include <QHash>
#include <QMetaObject>

#include "core/qtypes.h"

struct RegisteredType {
  const QMetaObject* meta;
  QString pyName;

  // Function to cast QEventBase* to this type
  std::function<const QEventBase*(const QEventBase*)> castFunc;
};

class PyTypeRegistry {
public:
  // register a Python-visible name and its corresponding C++ metaobject
  static void registerType(
    const QString &pyName,
    const QMetaObject *meta,
    std::function<const QEventBase*(const QEventBase*)> castFunc = nullptr
  );

  static void registerAll();

  // finders
  static const RegisteredType *findByPyName(const QString &pyName);
  static const RegisteredType *findByMeta(const QMetaObject *meta);
  static const QMetaObject* metaForPy(const QString& pyTypeName);

  // registry accessor
  static const QHash<QString, RegisteredType> &all();

private:
  static QHash<QString, RegisteredType> registry_;
};
