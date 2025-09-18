#pragma once

#include <QObject>
#include <QThread>
#include <QVector>
#include <QVariant>
#include <QMutex>

#include "interpreter.h"  // Snake

class DiamondDogs final : public QObject {
  Q_OBJECT

public:
  explicit DiamondDogs(QObject *parent = nullptr);
  ~DiamondDogs() override;

  // round-robin callFunction wrapper
  template<typename... Args>
  QVariant callFunction(const QString &funcName, Args&&... args) {
    const QVariantList argList{QVariant(std::forward<Args>(args))...};
    return callFunctionList(funcName, argList);
  }

private:
  QVariant callFunctionList(const QString &funcName, const QVariantList &args);

  QVector<QThread*> threads_;
  QVector<Snake*> snakes_;
  int nextIndex_;
  QMutex mutex_;
};
