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
static PyObject *py_is_debug(PyObject *self, PyObject *args);
static PyObject *py_interpreter_idx(PyObject *self, PyObject *args);

static PyMethodDef SnakeMethods[] = {
    {"get_accounts", py_get_accounts, METH_VARARGS, "Get accounts by UUIDs"},
    {"get_channels", py_get_channels, METH_VARARGS, "Get channels by UUIDs"},
    {"is_debug", py_is_debug, METH_NOARGS, "Returns True if compiled in debug mode"},
    {"interpreter_idx", py_interpreter_idx, METH_NOARGS, "Returns the Snake interpreter index"},
    {nullptr, nullptr, 0, nullptr} // sentinel
};

static struct PyModuleDef SnakeModule = {
    PyModuleDef_HEAD_INIT,
    "snake",
    "Snake C++ module",
    -1,
    SnakeMethods
};

Snake::Snake(QObject *parent) :
  QObject(parent), interp_(nullptr) {
}

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

  // inject snake module
  PyObject *pyModule = PyModule_Create(&SnakeModule);
  if (!pyModule) {
    qWarning() << "Failed to create snake module";
    emit started(false);
    return;
  }

  PyModule_AddObject(pyModule, "_cpp_instance", PyCapsule_New(this, "SnakePtr", nullptr));
  PyDict_SetItemString(main_dict, "snake", pyModule);

  const QString modulePath = g::pythonModulesDirectory;
  PyRun_SimpleString(
      QString("import sys; sys.path.append('%1'); from qircd import __qirc_call").arg(modulePath).toUtf8().constData());

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

  qDebug() << QString("Python interpreter %1 initialized").arg(QString::number(idx));
  emit started(true);
}

QHash<QByteArray, QSharedPointer<ModuleClass>> Snake::listModules() const {
  if (!interp_)
    return {};

  PyEval_RestoreThread(interp_->tstate);

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);

  PyObject *result = PyRun_String("qirc.list_modules()", Py_eval_input, main_dict, main_dict);

  QHash<QByteArray, QSharedPointer<ModuleClass>> modules;
  if (result) {
    QVariant obj = PyObjectToQVariant(result);
    const QJsonObject jsonModules = obj.toJsonObject();
    for (auto it = jsonModules.constBegin(); it != jsonModules.constEnd(); ++it)
      modules.insert(it.key().toUtf8(), ModuleClass::create_from_json(it.value().toObject()));

    Py_DECREF(result);
  } else {
    qWarning() << "Failed to list modules in interpreter" << idx;
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

// has initial support for asyncio
QVariant Snake::executeFunction(const QString &funcName, const QVariantList &args) const {
  qDebug() << "Python call in interpreter" << idx << args;
  QVariant result;

  if (!interp_) {
    qWarning() << "Interpreter not initialized";
    return result;
  }

  PyEval_RestoreThread(interp_->tstate);

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);
  PyObject *pyFunc = PyDict_GetItemString(main_dict, funcName.toUtf8().constData());

  if (!pyFunc || !PyCallable_Check(pyFunc)) {
    qWarning() << "Python function not found or not callable:" << funcName;
    interp_->tstate = PyEval_SaveThread();
    return result;
  }

  // build argument tuple
  PyObject *pyArgs = PyTuple_New(args.size());
  for (int i = 0; i < args.size(); ++i) {
    PyObject *pyObj = QVariantToPyObject(args[i]);
    PyTuple_SetItem(pyArgs, i, pyObj); // steals reference
  }

  // call function
  PyObject *pyResult = PyObject_CallObject(pyFunc, pyArgs);
  Py_DECREF(pyArgs);

  if (!pyResult) {
    PyErr_Print();
    qWarning() << "Python function call failed:" << funcName;
    interp_->tstate = PyEval_SaveThread();
    return result;
  }

  // handle async functions
  if (PyCoro_CheckExact(pyResult)) {
    PyObject *asyncio = PyImport_ImportModule("asyncio");
    if (!asyncio) {
      PyErr_Print();
      qWarning() << "Failed to import asyncio";
      Py_DECREF(pyResult);
      interp_->tstate = PyEval_SaveThread();
      return result;
    }

    PyObject *runFunc = PyObject_GetAttrString(asyncio, "run");
    Py_DECREF(asyncio);

    if (!runFunc || !PyCallable_Check(runFunc)) {
      PyErr_Print();
      qWarning() << "asyncio.run not callable";
      Py_XDECREF(runFunc);
      Py_DECREF(pyResult);
      interp_->tstate = PyEval_SaveThread();
      return result;
    }

    PyObject *awaited = PyObject_CallFunctionObjArgs(runFunc, pyResult, NULL);
    Py_DECREF(runFunc);
    Py_DECREF(pyResult);

    if (!awaited) {
      PyErr_Print();
      qWarning() << "Async function call failed:" << funcName;
      interp_->tstate = PyEval_SaveThread();
      return result;
    }

    result = PyObjectToQVariant(awaited);
    Py_DECREF(awaited);
  } else {
    result = PyObjectToQVariant(pyResult);
    Py_DECREF(pyResult);
  }

  interp_->tstate = PyEval_SaveThread();
  return result;
}

static PyObject *py_get_accounts(PyObject *self, PyObject *args) {
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

  return pyResult;
}

static PyObject *py_is_debug(PyObject *self, PyObject *args) {
#ifdef DEBUG
  Py_RETURN_TRUE;
#else
  Py_RETURN_FALSE;
#endif
}

static PyObject *py_interpreter_idx(PyObject *self, PyObject *args) {
  PyObject *capsule = PyObject_GetAttrString(self, "_cpp_instance");
  if (!capsule) {
    PyErr_SetString(PyExc_RuntimeError, "C++ instance not found");
    return nullptr;
  }

  const auto *snake = reinterpret_cast<Snake *>(PyCapsule_GetPointer(capsule, "SnakePtr"));
  if (!snake) {
    PyErr_SetString(PyExc_RuntimeError, "Failed to get C++ instance");
    return nullptr;
  }

  return PyLong_FromLong(snake->idx);
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
