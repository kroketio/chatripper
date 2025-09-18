#pragma once
// do not include Python.h here, only in .cpp
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

struct ThreadInterp; // forward declare

class Snake final : public QObject {
Q_OBJECT

public:
  explicit Snake(QObject *parent = nullptr);
  ~Snake() override;

  static QString version();
  void restart();

  template<typename... Args>
  QVariant callFunction(const QString &funcName, Args &&... args) {
    const QVariantList argList{QVariant(std::forward<Args>(args))...};
    return callFunctionList(funcName, argList);
  }

public slots:
  void start();

  // must be Q_INVOKABLE for invokeMethod
  Q_INVOKABLE QVariant executeFunction(const QString &funcName, const QVariantList &args) const;

private:
  // used by variadic template (callFunction())
  QVariant callFunctionList(const QString &funcName, const QVariantList &args);

  ThreadInterp* interp_;   // per-thread interpreter state
};

