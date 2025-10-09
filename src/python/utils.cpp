#include "utils.h"

QString GetStr(PyObject *dict, const char *name) {
  PyObject *val = PyDict_GetItemString(dict, name);
  return val && PyUnicode_Check(val) ? QString::fromUtf8(PyUnicode_AsUTF8(val)) : QString();
}

bool GetBool(PyObject *dict, const char *name) {
  PyObject *val = PyDict_GetItemString(dict, name);
  return val && PyObject_IsTrue(val);
}

QByteArray GetBytes(PyObject *dict, const char *name) {
  PyObject *val = PyDict_GetItemString(dict, name);
  if (val && PyBytes_Check(val))
    return {PyBytes_AsString(val), PyBytes_Size(val)};
  return {};
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

    case QMetaType::QByteArray: {
      const QByteArray bytes = var.toByteArray();
      return PyBytes_FromStringAndSize(bytes.constData(), bytes.size());
    }

    case QMetaType::QVariantList: {
      const QVariantList list = var.toList();
      PyObject* pyList = PyList_New(list.size());
      for (int i = 0; i < list.size(); ++i) {
        PyObject* item = QVariantToPyObject(list[i]);
        PyList_SetItem(pyList, i, item);  // steals ref
      }
      return pyList;
    }

    case QMetaType::QJsonObject: {
      qCritical() << "do not use QJsonObject for QVariantToPyObject - it is not supported";
      const QJsonObject obj = var.toJsonObject();
      return QVariantToPyObject(QVariant(obj.toVariantMap()));
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
  }

  else if (obj == Py_None) {
    return {};
  }

  return QString("<unsupported type>");
}

PyObject* convertListToPy(const QVariantList& list, PyObject* elemType) {
  PyObject* pyList = PyList_New(list.size());
  for (int i = 0; i < list.size(); ++i) {
    PyObject* item = convertVariantToPyAccordingToType(list[i], elemType);
    PyList_SetItem(pyList, i, item); // steals reference
  }
  return pyList;
}

PyObject* convertMapToPy(const QVariantMap& map, PyObject* valueType) {
  PyObject* pyDict = PyDict_New();
  for (auto it = map.begin(); it != map.end(); ++it) {
    PyObject* k = PyUnicode_FromString(it.key().toUtf8().constData());
    PyObject* v = convertVariantToPyAccordingToType(it.value(), valueType);
    PyDict_SetItem(pyDict, k, v);
    Py_DECREF(k);
    Py_DECREF(v);
  }
  return pyDict;
}

PyObject* convertVariantToPyAccordingToType(const QVariant& value, PyObject* pyType) {
  if (pyType == reinterpret_cast<PyObject*>(&PyUnicode_Type)) {
    if (value.canConvert<QString>()) return PyUnicode_FromString(value.toString().toUtf8().constData());
    else if (value.canConvert<QByteArray>()) return PyUnicode_FromString(value.toByteArray().constData());
  }
  else if (pyType == reinterpret_cast<PyObject*>(&PyBytes_Type)) {
    if (value.canConvert<QByteArray>()) {
      QByteArray ba = value.toByteArray();
      return PyBytes_FromStringAndSize(ba.constData(), ba.size());
    }
  }
  else if (pyType == reinterpret_cast<PyObject*>(&PyBool_Type)) return PyBool_FromLong(value.toBool());
  else if (pyType == reinterpret_cast<PyObject*>(&PyLong_Type)) return PyLong_FromLongLong(value.toLongLong());
  else if (pyType == reinterpret_cast<PyObject*>(&PyFloat_Type)) return PyFloat_FromDouble(value.toDouble());
  else if (PyList_Check(pyType) && value.metaType().id() == QMetaType::QVariantList)
    return convertListToPy(value.toList(), PyTuple_GetItem(pyType, 0));
  else if (PyDict_Check(pyType) && value.metaType().id() == QMetaType::QVariantMap)
    return convertMapToPy(value.toMap(), PyTuple_GetItem(pyType, 1));

  Py_INCREF(Py_None);
  return Py_None;
}

QVariant convertPyObjectToVariant(PyObject* obj) {
  if (!obj) return QVariant();

  // none
  if (obj == Py_None) return QVariant();

  // basic types
  if (PyBool_Check(obj)) return QVariant(static_cast<bool>(PyObject_IsTrue(obj)));
  if (PyLong_Check(obj)) return QVariant(static_cast<qlonglong>(PyLong_AsLongLong(obj)));
  if (PyFloat_Check(obj)) return QVariant(PyFloat_AsDouble(obj));
  if (PyUnicode_Check(obj)) {
    return QVariant(QString::fromUtf8(PyUnicode_AsUTF8(obj)));
  }
  if (PyBytes_Check(obj)) {
    char* data = nullptr;
    Py_ssize_t size = 0;
    if (PyBytes_AsStringAndSize(obj, &data, &size) == 0 && data != nullptr) {
      return QVariant(QByteArray(data, static_cast<int>(size)));
    }
    return QVariant(); // fallback if error
  }

  // list
  if (PyList_Check(obj)) {
    QVariantList list;
    Py_ssize_t len = PyList_Size(obj);
    for (Py_ssize_t i = 0; i < len; ++i) {
      PyObject* item = PyList_GetItem(obj, i); // borrowed reference
      list.append(convertPyObjectToVariant(item));
    }
    return list;
  }

  // dict
  if (PyDict_Check(obj)) {
    QVariantMap map;
    PyObject *key, *value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(obj, &pos, &key, &value)) {
      if (!PyUnicode_Check(key)) continue;
      QString k = PyUnicode_AsUTF8(key);
      map.insert(k, convertPyObjectToVariant(value));
    }
    return map;
  }

  // nested dataclass (has __annotations__)
  if (PyObject_HasAttrString(obj, "__annotations__")) {
    QVariantMap map;
    PyObject* annotations = PyObject_GetAttrString(obj, "__annotations__");
    if (annotations && PyDict_Check(annotations)) {
      PyObject *key, *pyType;
      Py_ssize_t pos = 0;
      while (PyDict_Next(annotations, &pos, &key, &pyType)) {
        if (!PyUnicode_Check(key)) continue;
        QString fieldName = PyUnicode_AsUTF8(key);
        PyObject* fieldValue = PyObject_GetAttrString(obj, fieldName.toUtf8().constData());
        map.insert(fieldName, convertPyObjectToVariant(fieldValue));
        Py_XDECREF(fieldValue);
      }
    }
    Py_XDECREF(annotations);
    return map;
  }

  // unknown type
  return QVariant();
}
