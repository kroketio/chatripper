#include "manager.h"
#include <QCoreApplication>
#include <QMetaObject>

SnakePit::SnakePit(QObject *parent) : QObject(parent), m_started_counter(0), next_index(0) {
  m_threads.resize(m_thread_count);
  m_snakes.resize(m_thread_count);

  for (int i = 0; i < m_thread_count; ++i) {
    auto *thread = new QThread(this);
    thread->setObjectName(QString("python_interpreter-%1").arg(QString::number(i+1)));
    const auto snake = new Snake();
    snake->setIndex(i);
    snake->moveToThread(thread);

    // track started interpreters
    connect(snake, &Snake::started, this, &SnakePit::onSnakesReady, Qt::UniqueConnection);

    connect(thread, &QThread::started, snake, &Snake::start);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, thread, &QThread::quit);
    connect(thread, &QThread::finished, snake, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    m_threads[i] = thread;
    m_snakes[i] = snake;

    thread->start();
  }
}

void SnakePit::onSnakesReady(const bool ok) {
  if (!ok) {
    qWarning() << "snake thread failed to start!";
    return;
  }

  if (++m_started_counter < m_thread_count)
    return;

  qDebug() << "all Python interpreters ready";

  if (!m_snakes.isEmpty()) {
    QMutexLocker locker(&mtx_refresh);
    m_modules = m_snakes[0]->listModules();
    calcActiveEvents();
    emit modulesRefreshed(m_modules);
  }

  emit allSnakesStarted();
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

  QMutexLocker locker(&mtx_refresh);
  m_modules = modules;
  calcActiveEvents();

  emit modulesRefreshed(m_modules);
}

bool SnakePit::enableModule(const QString &name) {
  bool ok = true;
  for (const Snake *s : m_snakes)
    ok &= s->enableModule(name);

  QMutexLocker locker(&mtx_refresh);

  const QByteArray key = name.toUtf8();
  if (m_modules.contains(key))
    m_modules[key]->enabled = true;

  calcActiveEvents();
  return ok;
}

bool SnakePit::disableModule(const QString &name) {
  bool ok = true;
  for (const Snake *s : m_snakes)
    ok &= s->disableModule(name);

  QMutexLocker locker(&mtx_refresh);

  const QByteArray key = name.toUtf8();
  if (m_modules.contains(key))
    m_modules[key]->enabled = false;

  calcActiveEvents();
  return ok;
}

void SnakePit::calcActiveEvents() {
  Flags<QIRCEvent> flags;
  Flags<QIRCEvent> flagsExclusive;
  for (auto it = m_modules.constBegin(); it != m_modules.constEnd(); ++it) {
    const auto &mod = it.value();
    if (!mod->enabled)
      continue;
    for (const auto &[event, method]: mod->handlers) {
      flags.set(event);
      if (mod->mode == QModuleMode::EXCLUSIVE)
        flagsExclusive.set(event);
    }
  }

  m_activeEvents = flags;
  m_activeExclusiveEvents = flagsExclusive;
}

QVariant SnakePit::callFunctionList(const QString &funcName, const QVariantList &args) {
  if (m_snakes.isEmpty())
    return {};

  const auto& ev = args.at(0);
  Snake *target = nullptr;

  QMutexLocker locker(&mtx_snake);
  if (ev.canConvert<QIRCEvent>())
    if (const auto event = ev.value<QIRCEvent>(); m_activeExclusiveEvents.has(event))
      target = m_snakes[0];  // event is exclusive to interpreter 0

  if (target == nullptr) {
    const int idx = next_index;
    next_index = (next_index + 1) % static_cast<int>(m_snakes.size());
    target = m_snakes[idx];
  }
  locker.unlock();

  QVariant returnValue;

  QMutexLocker locker_interpreter(&target->mtx_interpreter);
  QMetaObject::invokeMethod(
    target,
    "executeFunction",
    Qt::BlockingQueuedConnection,
    Q_RETURN_ARG(QVariant, returnValue),
    Q_ARG(QString, funcName),
    Q_ARG(QVariantList, args));

  return returnValue;
}

bool SnakePit::hasEventHandler(const QIRCEvent event) const {
  QReadLocker locker(&m_activeEventsLock);
  return m_activeEvents.has(event);
}

Flags<QIRCEvent> SnakePit::activeEvents() const {
  QReadLocker locker(&m_activeEventsLock);
  return m_activeEvents;
}

SnakePit::~SnakePit() {
  std::for_each(m_threads.begin(), m_threads.end(), [](QThread* t) {
    if (!t || !t->isRunning()) return;
    t->quit();
    t->wait();
  });
}
