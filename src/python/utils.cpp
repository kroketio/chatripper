#include "utils.h"

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