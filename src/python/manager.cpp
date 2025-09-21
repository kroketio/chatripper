#include "manager.h"
#include <QCoreApplication>
#include <QMetaObject>

Snakes::Snakes(QObject *parent) : QObject(parent) {
  constexpr int thread_count = 3;
  m_threads.resize(thread_count);
  m_snakes.resize(thread_count);
  m_startedCounter = 0;

  for (int i = 0; i < thread_count; ++i) {
    const auto thread = new QThread(this);
    const auto snake = new Snake();
    snake->setIndex(i);
    snake->moveToThread(thread);

    // track started interpreters
    connect(snake, &Snake::started, this, [this, thread_count](const bool ok) {
      if (!ok)
        qWarning() << "snake thread failed to start!";
      m_startedCounter++;
      if (m_startedCounter == thread_count) {
        qDebug() << "all Python interpreters ready";
        emit allSnakesStarted();
      }
    }, Qt::UniqueConnection);

    connect(thread, &QThread::started, snake, &Snake::start);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, thread, &QThread::quit);
    connect(thread, &QThread::finished, snake, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    m_threads[i] = thread;
    m_snakes[i] = snake;

    thread->start();
  }
}

void Snakes::restart() {
  if (m_snakes.isEmpty())
    return;

  QMutexLocker locker(&mtx_snake);
  m_startedCounter = 0;

  for (Snake *s: m_snakes) {
    QMetaObject::invokeMethod(s, "restart", Qt::BlockingQueuedConnection);
  }
}

QHash<QByteArray, QSharedPointer<ModuleClass>> Snakes::listModules() const {
  if (m_snakes.isEmpty())
    return {};
  return m_snakes[0]->modules();
}

void Snakes::refreshModulesAll() {
  if (m_snakes.isEmpty())
    return;

  QMutexLocker locker(&m_refreshMutex);
  m_refreshCounter = 0;

  for (Snake *s: m_snakes) {
    connect(s, &Snake::modulesRefreshed, this, [this](const QHash<QByteArray, QSharedPointer<ModuleClass>> & /*unused*/) {
      QMutexLocker locker(&m_refreshMutex);
      m_refreshCounter++;
      if (m_refreshCounter == m_snakes.size()) {
        // all threads refreshed; emit manager signal with first thread's modules
        emit modulesRefreshed(m_snakes[0]->modules());
      }
    }, Qt::UniqueConnection);

    // trigger refresh in Snake thread
    QMetaObject::invokeMethod(s, "refreshModules", Qt::QueuedConnection);
  }
}

bool Snakes::enableModule(const QString &name) {
  bool ok = true;
  for (Snake *s: m_snakes)
    ok &= s->enableModule(name);

  refreshModulesAll();
  return ok;
}

bool Snakes::disableModule(const QString &name) {
  bool ok = true;
  for (Snake *s: m_snakes)
    ok &= s->disableModule(name);

  refreshModulesAll();
  return ok;
}

QVariant Snakes::callFunctionList(const QString &funcName, const QVariantList &args) {
  QMutexLocker locker(&mtx_snake);

  if (m_snakes.isEmpty())
    return {};

  const int idx = next_index;
  next_index = (next_index + 1) % m_snakes.size();
  Snake *target = m_snakes[idx];

  QVariant returnValue;
  QMetaObject::invokeMethod(
      target,
      "executeFunction",
      Qt::BlockingQueuedConnection, // sync
      Q_RETURN_ARG(QVariant, returnValue),
      Q_ARG(QString, funcName),
      Q_ARG(QVariantList, args)
      );

  return returnValue;
}

// snake? snaakeeeeee
Snakes::~Snakes() {
  for (QThread *t: m_threads) {
    if (t && t->isRunning()) {
      t->quit();
      t->wait();
    }
  }
  // eliminated via finished->deleteLater()
}
