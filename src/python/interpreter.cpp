#include <Python.h> // before Qt headers else things explode
#include <QDebug>
#include "interpreter.h"

#include <QFileInfoList>
#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QFile>
#include <QMetaProperty>
#include <QSharedPointer>
#include <QVariant>

#include "ctx.h"
#include "config-circa.h"
#include "lib/utils.h"
#include "lib/logger_std/logger_std.h"
#include "python/type_registry.h"
#include "python/utils.h"

struct ThreadInterp {
  PyThreadState *tstate;
};

static PyObject *py_get_channels(PyObject *self, PyObject *args);
static PyObject *py_get_accounts(PyObject *self, PyObject *args);
static PyObject *py_is_debug(PyObject *self, PyObject *args);
static PyObject *py_version(PyObject *self, PyObject *args);
static PyObject *py_interpreter_idx(PyObject *self, PyObject *args);

static PyMethodDef SnakeMethods[] = {
    {"get_accounts", py_get_accounts, METH_VARARGS, "Get accounts by UUIDs"},
    {"get_channels", py_get_channels, METH_VARARGS, "Get channels by UUIDs"},
    {"is_debug", py_is_debug, METH_NOARGS, "Returns True if compiled in debug mode"},
    {"interpreter_idx", py_interpreter_idx, METH_NOARGS, "Returns the Snake interpreter index"},
    {"version", py_version, METH_NOARGS, "Returns cIRCa version"},
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

  // Ensure Python sees our module directory
  PyObject* sysModule = PyImport_ImportModule("sys");
  if (!sysModule) { PyErr_Print(); qWarning() << "Cannot import sys"; emit started(false); return; }

  PyObject* path = PyObject_GetAttrString(sysModule, "path");
  PyObject* pyDir = PyUnicode_FromString(g::pythonModulesDirectory.toUtf8().constData());
  PyList_Append(path, pyDir);
  Py_DECREF(pyDir);
  Py_DECREF(path);
  Py_DECREF(sysModule);

  // inject snake module
  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);

  // snake module
  PyObject *pyModule = PyModule_Create(&SnakeModule);
  if (!pyModule) { qWarning() << "Failed to create snake module"; emit started(false); return; }

  PyModule_AddObject(pyModule, "_cpp_instance", PyCapsule_New(this, "SnakePtr", nullptr));
  PyDict_SetItemString(main_dict, "snake", pyModule);

  // load qircd.events module (no caching needed)
  PyObject* eventsModule = PyImport_ImportModule("qircd.events");
  if (!eventsModule) {
    PyErr_Print();
    qWarning() << "Failed to import 'qircd.events'";
    // continue without event classes; they will be loaded on-demand
  } else {
    Py_DECREF(eventsModule);
  }

  PyRun_SimpleString("from qircd import __qirc_call");

  // set eventClasses dataclasses
  for (const auto&t: PyTypeRegistry::all()) {
    const auto name_str = t.pyName.toStdString();
    const auto name = name_str.c_str();

    PyObject *cls = PyObject_GetAttrString(eventsModule, name);
    if (!cls || !PyCallable_Check(cls)) {
      PyErr_Print();
      qWarning() << "Could not access class" << name;
      Py_XDECREF(cls);
      continue;
    }

    // store, increase refcount
    Py_INCREF(cls);
    eventClasses_[name] = cls;
  }

  // load user modules
  QDir dir(g::pythonModulesDirectory);
  dir.setNameFilters({"mod_*.py"});
  for (const QFileInfo &fileInfo : dir.entryInfoList(QDir::Files)) {
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

QVariant Snake::executeFunction(const QString &funcName, const QVariantList &args) {
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
  PyObject* pyArgs = PyTuple_New(args.size());
  for (int i = 0; i < args.size(); ++i) {
    const QVariant &arg = args[i];
    PyObject *pyObj = nullptr;

    for (const auto& entry : PyTypeRegistry::all()) {
      if (!entry.castFunc) continue;

      QByteArray metaName = entry.meta->className();
      const int targetTypeId = QMetaType::fromName((std::string("QSharedPointer<") + metaName.constData() + ">").c_str()).id();

      if (arg.userType() == targetTypeId) {
        pyObj = static_cast<PyObject*>(
          eventToPyHandle(*static_cast<const QSharedPointer<QEventBase>*>(arg.constData()))
        );
        if (pyObj == nullptr) {
          int wegg = 1;
        }

        break;
      }
    }

    if (!pyObj) pyObj = QVariantToPyObject(arg);
    PyTuple_SetItem(pyArgs, i, pyObj);
  }

  // call Python function
  PyObject *pyResult = PyObject_CallObject(pyFunc, pyArgs);
  Py_DECREF(pyArgs);

  if (!pyResult) {
    PyErr_Print();
    qWarning() << "Python function call failed:" << funcName;
    interp_->tstate = PyEval_SaveThread();
    return result;
  }

  // handle async function
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
      Py_XDECREF(runFunc);
      Py_DECREF(pyResult);
      qWarning() << "asyncio.run not callable";
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

    pyResult = awaited; // continue handling as normal result
  }

  // if result is a dataclass, recursively update existing QSharedPointers in args
  if (PyObject_HasAttrString(pyResult, "__annotations__")) {
    for (int i = 0; i < args.size(); ++i) {
      const QVariant &arg = args[i];

      for (const auto& entry : PyTypeRegistry::all()) {
        if (!entry.castFunc) continue;

        QByteArray metaName = entry.meta->className();
        const int typeId = QMetaType::fromName(
          QByteArray("QSharedPointer<") + entry.meta->className() + '>'
        ).id();

        if (arg.userType() == typeId) {
          auto evPtr = *static_cast<const QSharedPointer<QEventBase>*>(arg.constData());
          if (evPtr)
            updateGadgetFromPyDataclass(evPtr.data(), entry.meta, pyResult);
          break;
        }
      }
    }
  }

  Py_DECREF(pyResult);
  interp_->tstate = PyEval_SaveThread();

  // return the same second argument (if exists)
  return args.size() > 1 ? args.at(1) : QVariant();
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

static PyObject *py_version(PyObject *self, PyObject *args) {
  return PyUnicode_FromString(CIRCA_VERSION);
}

static PyObject *py_interpreter_idx(PyObject *self, PyObject *args) {
  PyObject *capsule = PyObject_GetAttrString(self, "_cpp_instance");
  if (!capsule) {
    PyErr_SetString(PyExc_RuntimeError, "C++ instance not found");
    return nullptr;
  }

  const auto *snake = static_cast<Snake *>(PyCapsule_GetPointer(capsule, "SnakePtr"));
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

void* Snake::eventToPyHandle(const QSharedPointer<QEventBase>& ev) const {
  if (!ev) return nullptr;

  const RegisteredType* regType = nullptr;

  // Iterate registry and find the first type whose castFunc succeeds
  for (const auto& entry : PyTypeRegistry::all()) {
    const RegisteredType& typeInfo = entry;
    if (typeInfo.castFunc && typeInfo.castFunc(ev.data())) {
      regType = &typeInfo;
      break;
    }
  }

  if (!regType)
    throw std::runtime_error("Unknown event type");

  const QString className = regType->pyName;
  const auto it = eventClasses_.find(className.toStdString());
  if (it == eventClasses_.end())
    throw std::runtime_error("No Python dataclass for " + className.toStdString());

  // Clear any pending Python exceptions before we start
  PyErr_Clear();

  PyObject* cls = reinterpret_cast<PyObject*>(it->second);
  PyObject* annotations = PyObject_GetAttrString(cls, "__annotations__");
  if (!annotations || !PyDict_Check(annotations)) {
    Py_XDECREF(annotations);
    PyErr_Print();
    qWarning() << "Dataclass has no __annotations__";
    return nullptr;
  }

  PyObject* dataclassFields = PyObject_GetAttrString(cls, "__dataclass_fields__");
  if (!dataclassFields || !PyDict_Check(dataclassFields)) {
    Py_XDECREF(dataclassFields);
    PyErr_Print();
    qWarning() << "Dataclass has no __dataclass_fields__";
    Py_XDECREF(annotations);
    return nullptr;
  }

  // get static meta-object for Q_GADGET
  const QMetaObject* metaObj = nullptr;
  for (const auto& entry : PyTypeRegistry::all()) {
    const RegisteredType& typeInfo = entry;
    if (typeInfo.castFunc && typeInfo.castFunc(ev.data())) {
      metaObj = typeInfo.meta;
      break;
    }
  }

  if (!metaObj) {
    qWarning() << "No metaObject found for event";
    Py_XDECREF(annotations);
    Py_XDECREF(dataclassFields);
    return nullptr;
  }

  PyObject* kwargs = PyDict_New();
  PyObject *key, *pyType;
  Py_ssize_t pos = 0;

  while (PyDict_Next(annotations, &pos, &key, &pyType)) {
    if (!PyUnicode_Check(key)) continue;
    QString fieldName = PyUnicode_AsUTF8(key);

    const int propIndex = metaObj->indexOfProperty(fieldName.toUtf8().constData());
    if (propIndex < 0) {
      qCritical() << "Unknown property in C++ event" << fieldName;
      continue;
    }

    QMetaProperty prop = metaObj->property(propIndex);
    QVariant value = prop.readOnGadget(ev.data());
    PyObject* pyValue = nullptr;

    // determine Python type name from annotation
    QString pyTypeName;
    if (PyType_Check(pyType)) {
      pyTypeName = reinterpret_cast<PyTypeObject*>(pyType)->tp_name;
    } else if (PyObject_HasAttrString(pyType, "__name__")) {
      PyObject* nameObj = PyObject_GetAttrString(pyType, "__name__");
      if (PyUnicode_Check(nameObj))
        pyTypeName = PyUnicode_AsUTF8(nameObj);
      Py_XDECREF(nameObj);
    }

    // handle QSharedPointer<T> that is actually a known QObject-derived type
    auto evtIt = eventClasses_.find(pyTypeName.toStdString());
    if (value.canConvert<QSharedPointer<QObject>>() && !pyTypeName.isEmpty() && evtIt != eventClasses_.end()) {
      auto obj = value.value<QSharedPointer<QObject>>();
      if (obj)
        pyValue = reinterpret_cast<PyObject*>(objectToPyDataclass(obj, pyTypeName));
    }

    // fallback: convert regular QVariant
    if (!pyValue)
      pyValue = convertVariantToPyAccordingToType(value, pyType);

    if (!pyValue) {
      Py_INCREF(Py_None);
      pyValue = Py_None;
    }

    // do not fill kwargs with None for this member when the dataclass member has
    // a default, or default_factory
    if (pyValue == Py_None) {
      PyObject* fieldInfo = PyDict_GetItemString(dataclassFields, fieldName.toUtf8().constData());
      if (fieldInfo) {
        PyObject* hasDefault = PyObject_GetAttrString(fieldInfo, "default");
        PyObject* hasDefaultFactory = PyObject_GetAttrString(fieldInfo, "default_factory");
        if ((hasDefault && hasDefault != Py_None) || hasDefaultFactory) {
          Py_XDECREF(hasDefault);
          Py_XDECREF(hasDefaultFactory);
          Py_DECREF(pyValue);
          continue;  // skip setting this kwarg, let dataclass factory handle it
        }
        Py_XDECREF(hasDefault);
        Py_XDECREF(hasDefaultFactory);
      }
    }

    PyDict_SetItemString(kwargs, fieldName.toUtf8().constData(), pyValue);
    Py_DECREF(pyValue);
  }

  PyObject* args = PyTuple_New(0);
  PyObject* instance = PyObject_Call(cls, args, kwargs);
  Py_DECREF(args);
  Py_DECREF(kwargs);
  Py_XDECREF(annotations);
  Py_XDECREF(dataclassFields);

  if (!instance) {
    PyErr_Print();
    qWarning() << "Failed to create Python dataclass instance for" << className;
    return nullptr;
  }

  return reinterpret_cast<void*>(instance);
}

void* Snake::objectToPyDataclass(const QSharedPointer<QObject>& obj, const QString& className) const {
  if (!obj) return nullptr;

  const auto it = eventClasses_.find(className.toStdString());
  if (it == eventClasses_.end()) {
    qWarning() << "No Python dataclass for" << className;
    return nullptr;
  }

  PyObject* cls = reinterpret_cast<PyObject*>(it->second);
  PyObject* annotations = PyObject_GetAttrString(cls, "__annotations__");
  if (!annotations || !PyDict_Check(annotations)) {
    Py_XDECREF(annotations);
    return nullptr;
  }

  const QMetaObject* metaObj = obj->metaObject();
  PyObject* kwargs = PyDict_New();

  PyObject *key, *pyType;
  Py_ssize_t pos = 0;
  while (PyDict_Next(annotations, &pos, &key, &pyType)) {
    if (!PyUnicode_Check(key)) continue;
    QString fieldName = PyUnicode_AsUTF8(key);

    const int propIndex = metaObj->indexOfProperty(fieldName.toUtf8().constData());
    if (propIndex < 0) continue;

    QMetaProperty prop = metaObj->property(propIndex);
    QVariant value = prop.read(obj.data());

    // handle type hints in the dataclass, e.g.: `host: Optional[str] = None`
    PyObject* actualType = pyType;
    if (PyObject_HasAttrString(pyType, "__origin__")) {
      PyObject* origin = PyObject_GetAttrString(pyType, "__origin__");
      PyObject* args = PyObject_GetAttrString(pyType, "__args__");
      if (origin && args && PyTuple_Check(args)) {
        Py_ssize_t n = PyTuple_Size(args);
        for (Py_ssize_t i = 0; i < n; ++i) {
          PyObject* t = PyTuple_GetItem(args, i);
          if (t != Py_None) {
            actualType = t;
            break;
          }
        }
      }
      Py_XDECREF(origin);
      Py_XDECREF(args);
    }

    PyObject* pyValue = convertVariantToPyAccordingToType(value, actualType);
    if (!pyValue) {
      Py_INCREF(Py_None);
      pyValue = Py_None;
    }

    PyDict_SetItemString(kwargs, fieldName.toUtf8().constData(), pyValue);
    Py_DECREF(pyValue);
  }

  PyObject* args = PyTuple_New(0);
  PyObject* instance = PyObject_Call(cls, args, kwargs);
  Py_DECREF(args);
  Py_DECREF(kwargs);
  Py_DECREF(annotations);

  return reinterpret_cast<void*>(instance);
}

void Snake::updateGadgetFromPyDataclass(void* targetGadget, const QMetaObject* metaObj, void* pyObjHandle) {
  if (!targetGadget) {
    qCritical() << "updateGadgetFromPyDataclass: targetGadget is null";
    return;
  }
  if (!metaObj) {
    qCritical() << "updateGadgetFromPyDataclass: metaObj is null";
    return;
  }
  if (!pyObjHandle) {
    qCritical() << "updateGadgetFromPyDataclass: pyObjHandle is null";
    return;
  }

  PyObject* pyObj = reinterpret_cast<PyObject*>(pyObjHandle);

  QStringList dirty_members;
  const QStringList dirty_ignore = {"QVariantMap", "QVariantList"};

  PyObject* val = PyObject_GetAttrString(pyObj, "_dirty");
  if (val) {
    if (PySet_Check(val)) {
      PyObject* iterator = PyObject_GetIter(val);
      PyObject* item;
      while ((item = PyIter_Next(iterator))) {
        dirty_members << PyUnicode_AsUTF8(item);
        Py_DECREF(item);
      }
      Py_DECREF(iterator);
    }
    Py_DECREF(val);
  }

  const char* cppClassName = metaObj->className();
  QString pyClassName;
  PyObject* cls = PyObject_GetAttrString(pyObj, "__class__");
  if (cls) {
    PyObject* pyClassNameObj = PyObject_GetAttrString(cls, "__name__");
    if (pyClassNameObj) {
      pyClassName = QString::fromUtf8(PyUnicode_AsUTF8(pyClassNameObj));
      Py_DECREF(pyClassNameObj);
    }
  }

  if (!cls) {
    qCritical() << "updateGadgetFromPyDataclass: Failed to get __class__ for C++ class" << cppClassName;
    return;
  }

  PyObject* annotations = PyDict_New();
  if (!annotations) {
    qCritical() << "updateGadgetFromPyDataclass: Failed to create annotations dict for class" << cppClassName;
    Py_DECREF(cls);
    return;
  }

  // gather annotations from MRO
  PyObject* mroCls = cls;
  Py_INCREF(mroCls);
  while (mroCls) {
    PyObject* clsAnn = PyObject_GetAttrString(mroCls, "__annotations__");
    if (clsAnn && PyDict_Check(clsAnn)) PyDict_Update(annotations, clsAnn);
    Py_XDECREF(clsAnn);

    PyObject* bases = PyObject_GetAttrString(mroCls, "__bases__");
    if (!bases || !PyTuple_Check(bases) || PyTuple_Size(bases) == 0) {
      Py_XDECREF(bases);
      break;
    }

    PyObject* firstBase = PyTuple_GetItem(bases, 0);
    Py_INCREF(firstBase);
    Py_DECREF(mroCls);
    mroCls = firstBase;
    Py_DECREF(bases);
  }
  Py_DECREF(mroCls);

  PyObject *key, *pyType;
  Py_ssize_t pos = 0;

  while (PyDict_Next(annotations, &pos, &key, &pyType)) {
    if (!PyUnicode_Check(key)) {
      qCritical() << "updateGadgetFromPyDataclass: Annotation key is not a string for class" << cppClassName;
      continue;
    }

    QString fieldName = PyUnicode_AsUTF8(key);
    int propIndex = metaObj->indexOfProperty(fieldName.toUtf8().constData());
    if (propIndex < 0) continue;

    QMetaProperty prop = metaObj->property(propIndex);
    QString typeName = QString::fromUtf8(prop.metaType().name());

    // nested QSharedPointer
    if (typeName.startsWith("QSharedPointer<")) {
      QVariant currentVar = prop.readOnGadget(targetGadget);
      if (!currentVar.canConvert<QSharedPointer<QObject>>()) {
        qCritical() << "Cannot convert property to QSharedPointer<QObject>: field" << fieldName;
        continue;
      }

      QSharedPointer<QObject> currentPtrVar = currentVar.value<QSharedPointer<QObject>>();
      void* currentPtr = currentPtrVar.data();
      if (!currentPtr) continue;

      PyObject* nestedPyObj = PyObject_GetAttrString(pyObj, fieldName.toUtf8().constData());
      if (!nestedPyObj || nestedPyObj == Py_None) {
        Py_XDECREF(nestedPyObj);
        continue;
      }

      QString pyTypeName;
      PyObject* pyTypeNameObj = PyObject_GetAttrString(pyType, "__name__");
      if (pyTypeNameObj) {
        pyTypeName = QString::fromUtf8(PyUnicode_AsUTF8(pyTypeNameObj));
        Py_DECREF(pyTypeNameObj);
      }

      const QMetaObject* nestedMeta = PyTypeRegistry::metaForPy(pyTypeName);
      if (nestedMeta) {
        updateGadgetFromPyDataclass(currentPtr, nestedMeta, nestedPyObj);
      } else {
        qCritical() << "Nested type not found in TypeRegistry:" << pyTypeName;
      }
      Py_DECREF(nestedPyObj);
      continue;
    }

    // skip non-dirty unless dict/list
    if (!dirty_members.contains(fieldName) && !dirty_ignore.contains(typeName)) continue;

    PyObject* pyValue = PyObject_GetAttrString(pyObj, fieldName.toUtf8().constData());
    if (!pyValue) continue;

    if (pyValue == Py_None) Py_INCREF(Py_None);  // safety
    QVariant value = convertPyObjectToVariant(pyValue);
    if (!prop.writeOnGadget(targetGadget, value)) {
      qCritical() << "Failed to write property:" << fieldName;
    }

    Py_DECREF(pyValue);
  }

  Py_DECREF(cls);
  Py_DECREF(annotations);
}
