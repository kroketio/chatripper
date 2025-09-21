#pragma once

#include <QObject>
#include <QThread>
#include <QVector>
#include <QVariant>
#include <QWaitCondition>
#include <QReadWriteLock>
#include <QMutex>

#include "lib/bitflags.h"
#include "interpreter.h"

// Round-robin for the various Python interpreter threads

class SnakePit final : public QObject {
Q_OBJECT

public:
  explicit SnakePit(QObject *parent = nullptr);
  ~SnakePit() override;

  void restart();
  QHash<QByteArray, QSharedPointer<ModuleClass>> listModules() const;
  bool enableModule(const QString &name);
  bool disableModule(const QString &name);
  void refreshModulesAll();

  // events
  Flags<QIRCEvent> activeEvents() const;
  bool hasEventHandler(QIRCEvent event) const;

  template<typename... Args>
  QVariant event(QIRCEvent ev, Args&&... args) {
    QVariantList argList;
    argList.append(static_cast<int>(ev));
    (argList.append(QVariant(std::forward<Args>(args))), ...);
    return callFunctionList("__qirc_call", argList);
  }

  template<typename... Args>
  QVariant callFunction(const QString &funcName, Args&&... args) {
    const QVariantList argList{QVariant(std::forward<Args>(args))...};
    return callFunctionList(funcName, argList);
  }

signals:
  void allSnakesStarted();
  void modulesRefreshed(const QHash<QByteArray, QSharedPointer<ModuleClass>> &modules);

private slots:
  void onSnakeStarted(bool ok);

private:
  QHash<QByteArray, QSharedPointer<ModuleClass>> m_modules;

  Flags<QIRCEvent> m_activeEvents;
  mutable QReadWriteLock m_activeEventsLock;

  QVariant callFunctionList(const QString &funcName, const QVariantList &args);

  QVector<QThread*> m_threads;
  QVector<Snake*> m_snakes;
  int next_index = 0;
  QMutex mtx_snake;

  int m_started_counter = 0;
  int m_refresh_counter = 0;
  mutable QMutex mtx_refresh;
};
