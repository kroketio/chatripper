#pragma once

#include <QObject>
#include <QThread>
#include <QVector>
#include <QVariant>
#include <QMutex>

#include "interpreter.h"

// Round-robin for the various Python interpreter threads

class Snakes final : public QObject {
  Q_OBJECT

public:
  explicit Snakes(QObject *parent = nullptr);
  ~Snakes() override;

  void restart();
  void refreshModulesAll();

  // callFunctionList wrapper
  template<typename... Args>
  QVariant callFunction(const QString &funcName, Args&&... args) {
    const QVariantList argList{QVariant(std::forward<Args>(args))...};
    return callFunctionList(funcName, argList);
  }

signals:
  void modulesRefreshed(const QJsonObject &modules);

private:
  QVariant callFunctionList(const QString &funcName, const QVariantList &args);

  QVector<QThread*> m_threads;
  QVector<Snake*> m_snakes;
  int next_index;
  QMutex mtx_snake;

  QJsonObject listModules() const;
  bool enableModule(const QString &name);
  bool disableModule(const QString &name);

  int m_refreshCounter = 0;
  mutable QMutex m_refreshMutex;
};
