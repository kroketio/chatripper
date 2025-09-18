#include <Python.h> // before Qt headers else things explode
#include <QDebug>
#include "interpreter.h"

#include <QFileInfoList>
#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QFile>

#include "lib/utils.h"

struct ThreadInterp {
    PyThreadState* tstate;
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

  // own GIL per interpreter
  config.gil = PyInterpreterConfig_OWN_GIL;

  PyThreadState* tstate = nullptr;
  const PyStatus status = Py_NewInterpreterFromConfig(&tstate, &config);
  if (PyStatus_Exception(status)) {
    qWarning() << "Failed to create sub-interpreter:" << status.err_msg;
    return;
  }

  interp_ = new ThreadInterp{tstate};

  // make the sub-interpreter current
  PyThreadState_Swap(interp_->tstate);

#ifdef DEBUG
  const QString modulePath = "/home/dsc/CLionProjects/chat/server/python/modules";
#else
  const QString modulePath = g::pythonModulesDirectory;
#endif
  PyRun_SimpleString(QString("import sys; sys.path.append('%1')").arg(modulePath).toUtf8().constData());

  QDir dir(modulePath);
  QStringList filters{"*.py"};
  dir.setNameFilters(filters);
  QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files);

  for (const QFileInfo &fileInfo : fileList) {
    if (!fileInfo.fileName().startsWith("mod_"))
      continue;

    QFile py_file(fileInfo.absoluteFilePath());
    if (py_file.open(QIODevice::ReadOnly)) {
      QByteArray content = py_file.readAll();
      py_file.close();

      if (PyRun_SimpleString(content.constData()) != 0)
        qWarning() << "Error executing" << fileInfo.fileName();

      // qDebug() << "Executed" << fileInfo.fileName();
    }
  }

  // release the GIL when done
  interp_->tstate = PyEval_SaveThread();
}

void Snake::restart() {
  if (interp_) {
    PyEval_RestoreThread(interp_->tstate);
    Py_EndInterpreter(interp_->tstate);
    delete interp_;
    interp_ = nullptr;
  }

  start();  // recreate sub-interpreter
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

// QVariant -> PyObject* (recursive)
PyObject* QVariantToPyObject(const QVariant &var) {
  switch (var.typeId()) {
    case QMetaType::Int:
      return PyLong_FromLong(var.toInt());

    case QMetaType::Double:
      return PyFloat_FromDouble(var.toDouble());

    case QMetaType::Bool:
      return PyBool_FromLong(var.toBool());

    case QMetaType::QString:
      return PyUnicode_FromString(var.toString().toUtf8().constData());

    case QMetaType::QVariantList: {
      const QVariantList list = var.toList();
      PyObject* pyList = PyList_New(list.size());
      for (int i = 0; i < list.size(); ++i) {
        PyObject* item = QVariantToPyObject(list[i]);
        PyList_SetItem(pyList, i, item);  // steals ref
      }
      return pyList;
    }

    case QMetaType::QVariantMap: {
      const QVariantMap map = var.toMap();
      PyObject* pyDict = PyDict_New();
      for (auto it = map.begin(); it != map.end(); ++it) {
        PyObject* key = PyUnicode_FromString(it.key().toUtf8().constData());
        PyObject* value = QVariantToPyObject(it.value());
        PyDict_SetItem(pyDict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
      }
      return pyDict;
    }

    default:
      Py_RETURN_NONE;
  }
}

// PyObject* -> QVariant (recursive)
QVariant PyObjectToQVariant(PyObject *obj) {
  if (PyLong_Check(obj)) {
    return static_cast<int>(PyLong_AsLong(obj));
  } else if (PyFloat_Check(obj)) {
    return PyFloat_AsDouble(obj);
  } else if (PyUnicode_Check(obj)) {
    return QString::fromUtf8(PyUnicode_AsUTF8(obj));
  } else if (PyBool_Check(obj)) {
    return obj == Py_True;
  } else if (PyList_Check(obj)) {
    QVariantList list;
    Py_ssize_t size = PyList_Size(obj);
    for (Py_ssize_t i = 0; i < size; ++i) {
      PyObject *item = PyList_GetItem(obj, i); // borrowed ref
      list.append(PyObjectToQVariant(item));
    }
    return list;
  } else if (PyDict_Check(obj)) {
    QVariantMap map;
    PyObject *key, *value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(obj, &pos, &key, &value)) {
      QString qkey = QString::fromUtf8(PyUnicode_AsUTF8(key));
      map[qkey] = PyObjectToQVariant(value);
    }
    return map;
  } else if (obj == Py_None) {
    return {};
  } else {
    return QString("<unsupported type>");
  }
}

QVariant Snake::executeFunction(const QString &funcName, const QVariantList &args) const {
  QVariant result;
  if (!interp_) {
    qWarning() << "Interpreter not initialized";
    return result;
  }

  PyEval_RestoreThread(interp_->tstate);

  PyObject* mainModule = PyImport_AddModule("__main__");
  PyObject* mainDict = PyModule_GetDict(mainModule);
  PyObject* pyFunc = PyDict_GetItemString(mainDict, funcName.toUtf8().constData());

  if (pyFunc && PyCallable_Check(pyFunc)) {
    PyObject* pyArgs = PyTuple_New(args.size());
    for (int i = 0; i < args.size(); ++i) {
      PyObject* pyObj = QVariantToPyObject(args[i]);
      PyTuple_SetItem(pyArgs, i, pyObj); // steals reference
    }

    PyObject* pyResult = PyObject_CallObject(pyFunc, pyArgs);
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
