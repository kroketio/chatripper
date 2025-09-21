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
class Snake final : public QObject {
Q_OBJECT

public:
  explicit Snake(QObject *parent = nullptr);
  ~Snake() override;

  void setIndex(const int idx) { m_idx = idx; }
  int idx() const { return m_idx; }

  static QString version();
  void restart();

  QHash<QByteArray, QSharedPointer<ModuleClass>> modules() const;
  bool enableModule(const QString &name);
  bool disableModule(const QString &name);

  template<typename... Args>
  QVariant callFunction(const QString &funcName, Args &&... args) {
    const QVariantList argList{QVariant(std::forward<Args>(args))...};
    return callFunctionList(funcName, argList);
  }

public slots:
  void start();
  Q_INVOKABLE void refreshModules();

  // must be Q_INVOKABLE for invokeMethod
  Q_INVOKABLE QVariant executeFunction(const QString &funcName, const QVariantList &args) const;

signals:
  void started(bool ok);
  void modulesRefreshed(const QHash<QByteArray, QSharedPointer<ModuleClass>> &modules);

private:
  ThreadInterp* interp_;
  int m_idx = -1;

  // used by variadic template (callFunction())
  QVariant callFunctionList(const QString &funcName, const QVariantList &args);

  QHash<QByteArray, QSharedPointer<ModuleClass>> modules_;   // qirc.list_modules()
  mutable QMutex mtx_modules;
};

