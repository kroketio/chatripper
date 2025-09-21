#include <Python.h> // before Qt headers else things explode
#include <QDebug>
#include "interpreter.h"

#include <QFileInfoList>
#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QFile>

#include "ctx.h"
#include "lib/utils.h"
#include "lib/logger_std/logger_std.h"
#include "python/utils.h"

struct ThreadInterp {
  PyThreadState *tstate;
};

static PyObject *py_get_channels(PyObject *self, PyObject *args);
static PyObject *py_get_accounts(PyObject *self, PyObject *args);

static PyMethodDef SnakeMethods[] = {
    {"get_accounts", py_get_accounts, METH_VARARGS, "Get accounts by UUIDs"},
    {"get_channels", py_get_channels, METH_VARARGS, "Get channels by UUIDs"},
    {nullptr, nullptr, 0, nullptr} // sentinel
};

static struct PyModuleDef SnakeModule = {
    PyModuleDef_HEAD_INIT,
    "snake", // module name
    "Snake C++ module",
    -1,
    SnakeMethods
};

Snake::Snake(QObject *parent) : QObject(parent), interp_(nullptr) {}

void Snake::start() {
  PyInterpreterConfig config{};
  memset(&config, 0, sizeof(config));

  // https://docs.python.org/3/c-api/init.html#c.PyInterpreterConfig.use_main_obmalloc
  // If use_main_obmalloc is 0 then the sub-interpreter will use its own 'object'
  // allocator state. Otherwise, it will use (share) the main interpreter.
  // If use_main_obmalloc is 0 then check_multi_interp_extensions must be 1 (non-zero).
  config.use_main_obmalloc = 0;
  config.check_multi_interp_extensions = 1;
  config.allow_threads = 1;
  config.allow_fork = 1;
  config.allow_exec = 1;
  config.gil = PyInterpreterConfig_OWN_GIL;

  PyThreadState *tstate = nullptr;
  const PyStatus status = Py_NewInterpreterFromConfig(&tstate, &config);
  if (PyStatus_Exception(status)) {
    qWarning() << "Failed to create sub-interpreter:" << status.err_msg;
    emit started(false);
    return;
  }

  interp_ = new ThreadInterp{tstate};

  // make the sub-interpreter current
  PyThreadState_Swap(interp_->tstate);

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);

  // inject interpreter idx
  PyObject* pyIdx = PyLong_FromLong(m_idx);  // idx_ is the Snake interpreter index
  PyDict_SetItemString(main_dict, "INTERPRETER_IDX", pyIdx);
  Py_DECREF(pyIdx);

  // inject snake module
  PyObject *pyModule = PyModule_Create(&SnakeModule);
  if (!pyModule) {
    qWarning() << "Failed to create snake module";
    emit started(false);
    return;
  }

  PyDict_SetItemString(main_dict, "snake", pyModule);

#ifdef DEBUG
  const QString modulePath = "/home/dsc/CLionProjects/chat/server/python/modules";
#else
    const QString modulePath = g::pythonModulesDirectory;
#endif
  PyRun_SimpleString(QString("import sys; sys.path.append('%1'); from qircd import __qirc_call").arg(modulePath).toUtf8().constData());

  // load user modules
  QDir dir(modulePath);
  const QStringList filters{"*.py"};
  dir.setNameFilters(filters);
  QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files);

  for (const QFileInfo &fileInfo: fileList) {
    if (!fileInfo.fileName().startsWith("mod_"))
      continue;

    QFile py_file(fileInfo.absoluteFilePath());
    if (!py_file.open(QIODevice::ReadOnly)) {
      qWarning() << "Could not open" << fileInfo.fileName();
      continue;
    }

    QByteArray content = py_file.readAll();
    py_file.close();

    if (PyRun_SimpleString(content.constData()) != 0) {
      qWarning() << "Error executing" << fileInfo.fileName();
    }
  }

  // release GIL
  interp_->tstate = PyEval_SaveThread();

  qDebug() << QString("Python interpreter %1 initialized").arg(m_idx);
  emit started(true);
}

QHash<QByteArray, QSharedPointer<ModuleClass>> Snake::listModules() const {
  if (!interp_)
    return {};

  PyEval_RestoreThread(interp_->tstate);

  PyObject* main_module = PyImport_AddModule("__main__");
  PyObject* main_dict = PyModule_GetDict(main_module);

  PyObject* result = PyRun_String("qirc.list_modules()", Py_eval_input, main_dict, main_dict);

  QHash<QByteArray, QSharedPointer<ModuleClass>> modules;
  if (result) {
    QVariant obj = PyObjectToQVariant(result);
    const QJsonObject jsonModules = obj.toJsonObject();
    for (auto it = jsonModules.constBegin(); it != jsonModules.constEnd(); ++it)
      modules.insert(it.key().toUtf8(), ModuleClass::create_from_json(it.value().toObject()));

    Py_DECREF(result);
  } else {
    qWarning() << "Failed to list modules in interpreter" << m_idx;
  }

  interp_->tstate = PyEval_SaveThread();
  return modules;
}

void Snake::restart() {
  if (interp_) {
    PyEval_RestoreThread(interp_->tstate);
    Py_EndInterpreter(interp_->tstate);
    delete interp_;
    interp_ = nullptr;
  }

  start(); // recreate sub-interpreter
}

QVariant Snake::callFunctionList(const QString &funcName, const QVariantList &args) {
  QVariant result;
  QMetaObject::invokeMethod(
      this, "executeFunction",
      Qt::BlockingQueuedConnection,
      Q_RETURN_ARG(QVariant, result),
      Q_ARG(QString, funcName),
      Q_ARG(QVariantList, args));
  return result;
}

bool Snake::enableModule(const QString &name) const {
  if (!interp_)
    return false;

  PyEval_RestoreThread(interp_->tstate);

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);
  const QString code = QString("qirc.enable_module('%1')").arg(name);

  PyObject *result = PyRun_String(code.toUtf8().constData(), Py_eval_input, main_dict, main_dict);
  const bool ok = result != nullptr;
  Py_XDECREF(result);

  interp_->tstate = PyEval_SaveThread();

  return ok;
}

bool Snake::disableModule(const QString &name) const {
  if (!interp_)
    return false;

  PyEval_RestoreThread(interp_->tstate);

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);
  const QString code = QString("qirc.disable_module('%1')").arg(name);

  PyObject *result = PyRun_String(code.toUtf8().constData(), Py_eval_input, main_dict, main_dict);
  const bool ok = result != nullptr;
  Py_XDECREF(result);

  interp_->tstate = PyEval_SaveThread();

  return ok;
}

QString Snake::version() {
  return QString::fromStdString(std::string(Py_GetVersion()));
}

Snake::~Snake() {
  if (interp_) {
    // Acquire GIL for cleanup
    PyEval_RestoreThread(interp_->tstate);
    Py_EndInterpreter(interp_->tstate);
    delete interp_;
    interp_ = nullptr;
  }
}

QVariant Snake::executeFunction(const QString &funcName, const QVariantList &args) const {
  qDebug() << "Python call in interpreter" << m_idx << args;
  QVariant result;
  if (!interp_) {
    qWarning() << "Interpreter not initialized";
    return result;
  }

  PyEval_RestoreThread(interp_->tstate);

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);
  PyObject *pyFunc = PyDict_GetItemString(main_dict, funcName.toUtf8().constData());

  if (pyFunc && PyCallable_Check(pyFunc)) {
    PyObject *pyArgs = PyTuple_New(args.size());
    for (int i = 0; i < args.size(); ++i) {
      PyObject *pyObj = QVariantToPyObject(args[i]);
      PyTuple_SetItem(pyArgs, i, pyObj); // steals reference
    }

    PyObject *pyResult = PyObject_CallObject(pyFunc, pyArgs);
    Py_DECREF(pyArgs);

    if (pyResult) {
      result = PyObjectToQVariant(pyResult);
      Py_DECREF(pyResult);
    } else {
      PyErr_Print();
      qWarning() << "Python function call failed:" << funcName;
    }
  } else {
    qWarning() << "Python function not found:" << funcName;
  }

  interp_->tstate = PyEval_SaveThread();
  return result;
}

static PyObject *py_get_accounts(PyObject *self, PyObject *args) {
  CLOCK_MEASURE_START(wow);
  PyObject *pyList;
  if (!PyArg_ParseTuple(args, "O!", &PyList_Type, &pyList)) {
    return nullptr;
  }

  QList<QByteArray> uuids;
  Py_ssize_t len = PyList_Size(pyList);
  for (Py_ssize_t i = 0; i < len; ++i) {
    PyObject *item = PyList_GetItem(pyList, i);
    if (!PyUnicode_Check(item)) {
      PyErr_SetString(PyExc_TypeError, "UUIDs must be strings");
      return nullptr;
    }
    uuids.append(QByteArray(PyUnicode_AsUTF8(item)));
  }

  QList<QVariantMap> accounts = g::ctx->getAccountsByUUIDs(uuids);
  PyObject *pyResult = PyList_New(accounts.size());
  for (int i = 0; i < accounts.size(); ++i) {
    const QVariantMap &map = accounts[i];
    PyObject *pyDict = PyDict_New();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
      PyDict_SetItemString(pyDict, it.key().toUtf8().constData(),
                           PyUnicode_FromString(it.value().toString().toUtf8().constData()));
    }
    PyList_SetItem(pyResult, i, pyDict); // steals reference
  }

  CLOCK_MEASURE_END(wow, "wowowowow");
  return pyResult;
}

static PyObject *py_get_channels(PyObject *self, PyObject *args) {
  PyObject *pyList;
  if (!PyArg_ParseTuple(args, "O!", &PyList_Type, &pyList)) {
    return nullptr;
  }

  QList<QByteArray> uuids;
  Py_ssize_t len = PyList_Size(pyList);
  for (Py_ssize_t i = 0; i < len; ++i) {
    PyObject *item = PyList_GetItem(pyList, i);
    if (!PyUnicode_Check(item)) {
      PyErr_SetString(PyExc_TypeError, "UUIDs must be strings");
      return nullptr;
    }
    uuids.append(QByteArray(PyUnicode_AsUTF8(item)));
  }

  QList<QVariantMap> channels = g::ctx->getChannelsByUUIDs(uuids);
  PyObject *pyResult = PyList_New(channels.size());
  for (int i = 0; i < channels.size(); ++i) {
    const QVariantMap &map = channels[i];
    PyObject *pyDict = PyDict_New();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
      PyDict_SetItemString(pyDict, it.key().toUtf8().constData(),
                           PyUnicode_FromString(it.value().toString().toUtf8().constData()));
    }
    PyList_SetItem(pyResult, i, pyDict);
  }

  return pyResult;
}
