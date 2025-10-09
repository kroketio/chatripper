#pragma once

// do not include Python.h here, only in .cpp
#include <QObject>
#include <QString>
#include <QVariant>
#include <QJsonObject>
#include <QVariantList>
#include <QMutex>

#include "module.h"

struct ThreadInterp;
struct _object;
using PyObjectHandle = _object*;
class Snake final : public QObject {
Q_OBJECT

public:
  explicit Snake(QObject *parent = nullptr);
  ~Snake() override;
  QMutex mtx_interpreter;
  int idx = -1;

  [[nodiscard]] QHash<QByteArray, QSharedPointer<ModuleClass>> listModules() const;

  static QString version();
  void restart();

  [[nodiscard]] bool enableModule(const QString &name) const;
  [[nodiscard]] bool disableModule(const QString &name) const;

  template<typename... Args>
  QVariant callFunction(const QString &funcName, Args &&... args) {
    const QVariantList argList{QVariant(std::forward<Args>(args))...};
    return callFunctionList(funcName, argList);
  }

public slots:
  void start();

  // must be Q_INVOKABLE for invokeMethod
  Q_INVOKABLE [[nodiscard]] QVariant executeFunction(const QString &funcName, const QVariantList &args);

signals:
  void started(bool ok);

private:
  ThreadInterp* interp_;
  std::unordered_map<std::string, PyObjectHandle> eventClasses_;

  void* eventToPyHandle(const QSharedPointer<QEventBase>& ev) const;

  // used by variadic template (callFunction())
  QVariant callFunctionList(const QString &funcName, const QVariantList &args);

  void* objectToPyDataclass(const QSharedPointer<QObject>& obj, const QString& className) const;
  void updateGadgetFromPyDataclass(void* targetGadget, const QMetaObject* metaObj, void* pyObjHandle);
};

