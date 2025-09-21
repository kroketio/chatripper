#pragma once

#include <QObject>
#include <QThread>
#include <QVector>
#include <QVariant>
#include <QWaitCondition>
#include <QMutex>

#include "interpreter.h"

// Round-robin for the various Python interpreter threads

class Snakes final : public QObject {
Q_OBJECT

public:
  explicit Snakes(QObject *parent = nullptr);
  ~Snakes() override;

  void restart();
  QHash<QByteArray, QSharedPointer<ModuleClass>> listModules() const;
  bool enableModule(const QString &name);
  bool disableModule(const QString &name);
  void refreshModulesAll();

  // callFunctionList wrapper
  template<typename... Args>
  QVariant callFunction(const QString &funcName, Args&&... args) {
    const QVariantList argList{QVariant(std::forward<Args>(args))...};
    return callFunctionList(funcName, argList);
  }

signals:
  void allSnakesStarted();
  void modulesRefreshed(const QHash<QByteArray, QSharedPointer<ModuleClass>> &modules);

private:
  QVariant callFunctionList(const QString &funcName, const QVariantList &args);

  QVector<QThread*> m_threads;
  QVector<Snake*> m_snakes;
  int next_index = 0;
  QMutex mtx_snake;

  int m_startedCounter = 0;
  int m_refreshCounter = 0;
  mutable QMutex m_refreshMutex;
};
