#include "manager.h"
#include <QCoreApplication>
#include <QMetaObject>

SnakePit::SnakePit(QObject *parent) : QObject(parent), m_started_counter(0), next_index(0) {
  constexpr int thread_count = 3;
  m_threads.resize(thread_count);
  m_snakes.resize(thread_count);

  for (int i = 0; i < thread_count; ++i) {
    auto *thread = new QThread(this);
    const auto snake = new Snake();
    snake->setIndex(i);
    snake->moveToThread(thread);

    // track started interpreters
    connect(snake, &Snake::started, this, &SnakePit::onSnakeStarted, Qt::UniqueConnection);

    connect(thread, &QThread::started, snake, &Snake::start);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, thread, &QThread::quit);
    connect(thread, &QThread::finished, snake, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    m_threads[i] = thread;
    m_snakes[i] = snake;

    thread->start();
  }
}

void SnakePit::onSnakeStarted(bool ok) {
  if (!ok) {
    qWarning() << "snake thread failed to start!";
  }

  m_started_counter++;

  constexpr int thread_count = 3; // keep in sync with your constructor
  if (m_started_counter == thread_count) {
    qDebug() << "all Python interpreters ready";

    if (!m_snakes.isEmpty()) {
      QHash<QByteArray, QSharedPointer<ModuleClass>> modules = m_snakes[0]->listModules();
      Flags<QIRCEvent> activeEvents;

      for (auto it = modules.constBegin(); it != modules.constEnd(); ++it) {
        const auto &module = it.value();
        if (!module->enabled)
          continue;
        for (const auto &[event, method] : module->handlers)
          activeEvents.set(event);
      }

      QMutexLocker locker(&mtx_refresh);
      m_modules = modules;
      m_activeEvents = activeEvents;

      emit modulesRefreshed(m_modules);
    }

    emit allSnakesStarted();
  }
}


void SnakePit::restart() {
  if (m_snakes.isEmpty())
    return;

  QMutexLocker locker(&mtx_snake);
  m_started_counter = 0;

  for (Snake *s: m_snakes)
    QMetaObject::invokeMethod(s, "restart", Qt::BlockingQueuedConnection);
}

QHash<QByteArray, QSharedPointer<ModuleClass>> SnakePit::listModules() const {
  if (m_snakes.isEmpty())
    return {};
  return m_snakes[0]->listModules();
}

void SnakePit::refreshModulesAll() {
  if (m_snakes.isEmpty())
    return;

  const QHash<QByteArray, QSharedPointer<ModuleClass>> modules = m_snakes[0]->listModules();
  Flags<QIRCEvent> activeEvents;

  for (auto it = modules.constBegin(); it != modules.constEnd(); ++it) {
    const auto &module = it.value();

    if (!module->enabled)
      continue;

    for (const auto &[event, method] : module->handlers)
      activeEvents.set(event);
  }

  {
    QMutexLocker locker(&mtx_refresh);
    m_modules = modules;
    m_activeEvents = activeEvents;
  }

  emit modulesRefreshed(m_modules);
}

bool SnakePit::enableModule(const QString &name) {
  bool ok = true;
  for (Snake *s : m_snakes)
    ok &= s->enableModule(name);

  QMutexLocker locker(&mtx_refresh);

  if (m_modules.contains(name.toUtf8()))
    m_modules[name.toUtf8()]->enabled = true;

  Flags<QIRCEvent> activeEvents;
  for (const auto &mod : m_modules) {
    if (!mod->enabled)
      continue;

    for (const auto &[event, method] : mod->handlers)
      activeEvents.set(event);
  }
  m_activeEvents = activeEvents;

  return ok;
}

bool SnakePit::disableModule(const QString &name) {
  bool ok = true;
  for (const Snake *s : m_snakes)
    ok &= s->disableModule(name);

  QMutexLocker locker(&mtx_refresh);

  if (m_modules.contains(name.toUtf8()))
    m_modules[name.toUtf8()]->enabled = false;

  Flags<QIRCEvent> activeEvents;
  for (const auto &mod : m_modules) {
    if (!mod->enabled)
      continue;
    for (const auto &[event, method] : mod->handlers)
      activeEvents.set(event);
  }
  m_activeEvents = activeEvents;

  return ok;
}

QVariant SnakePit::callFunctionList(const QString &funcName, const QVariantList &args) {
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
    Qt::BlockingQueuedConnection,
    Q_RETURN_ARG(QVariant, returnValue),
    Q_ARG(QString, funcName),
    Q_ARG(QVariantList, args));

  return returnValue;
}

bool SnakePit::hasEventHandler(QIRCEvent event) const {
  QReadLocker locker(&m_activeEventsLock);
  return m_activeEvents.has(event);
}

Flags<QIRCEvent> SnakePit::activeEvents() const {
  QReadLocker locker(&m_activeEventsLock);
  return m_activeEvents;
}

SnakePit::~SnakePit() {
  for (QThread *t : m_threads) {
    if (t && t->isRunning()) {
      t->quit();
      t->wait();
    }
  }
}
