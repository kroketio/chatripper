#include "manager.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QDebug>

DiamondDogs::DiamondDogs(QObject *parent) : QObject(parent), nextIndex_(0) {
  constexpr int thread_count = 4;
  threads_.resize(thread_count);
  snakes_.resize(thread_count);

  for (int i = 0; i < thread_count; ++i) {
    const auto thread = new QThread(this);
    const auto snake = new Snake();

    snake->moveToThread(thread);
    connect(thread, &QThread::started, snake, &Snake::start);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, thread, &QThread::quit);
    connect(thread, &QThread::finished, snake, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    threads_[i] = thread;
    snakes_[i] = snake;

    thread->start();
  }
}

QVariant DiamondDogs::callFunctionList(const QString &funcName, const QVariantList &args) {
  QMutexLocker locker(&mutex_);

  if (snakes_.isEmpty())
    return {};

  const int idx = nextIndex_;
  nextIndex_ = (nextIndex_ + 1) % snakes_.size();
  Snake* target = snakes_[idx];

  QVariant returnValue;
  QMetaObject::invokeMethod(
    target,
    "executeFunction",
    Qt::BlockingQueuedConnection,  // sync
    Q_RETURN_ARG(QVariant, returnValue),
    Q_ARG(QString, funcName),
    Q_ARG(QVariantList, args)
  );

  return returnValue;
}

// snake? snaakeeeeee
DiamondDogs::~DiamondDogs() {
  for (QThread* t : threads_) {
    if (t->isRunning()) {
      t->quit();
      t->wait();
    }
  }
  // eliminated via finished->deleteLater()
}
