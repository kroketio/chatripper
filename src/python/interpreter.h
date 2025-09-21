#pragma once
// do not include Python.h here, only in .cpp
#include <QObject>
#include <QString>
#include <QVariant>
#include <QJsonObject>
#include <QVariantList>
#include <QMutex>

struct ThreadInterp;
class Snake final : public QObject {
Q_OBJECT

public:
  explicit Snake(QObject *parent = nullptr);
  ~Snake() override;

  static QString version();
  void restart();

  QJsonObject modules() const;
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
  void modulesRefreshed(const QJsonObject &modules);

private:
  ThreadInterp* interp_;

  // used by variadic template (callFunction())
  QVariant callFunctionList(const QString &funcName, const QVariantList &args);

  QJsonObject modules_;   // qirc.list_modules()
  mutable QMutex mtx_modules;
};

