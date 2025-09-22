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

  else if (PyObject_HasAttrString(obj, "__dataclass_fields__")) {
    PyObject *cls = PyObject_GetAttrString(obj, "__class__");
    QString className;
    if (cls) {
      PyObject *name = PyObject_GetAttrString(cls, "__name__");
      if (name) {
        className = QString::fromUtf8(PyUnicode_AsUTF8(name));
        Py_DECREF(name);
      }
      Py_DECREF(cls);
    }

    if (className == "Message") {
      return QVariant::fromValue(PyDataclassToQMessage(obj));
    } else if (className == "AuthUserResult") {
      return QVariant::fromValue(PyDataclassToQAuthUserResult(obj));
    } else {
      // fallback: return __dict__ as QVariantMap
      PyObject *dict = PyObject_GetAttrString(obj, "__dict__");
      if (dict && PyDict_Check(dict)) {
        QVariantMap map;
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(dict, &pos, &key, &value)) {
          QString qkey = QString::fromUtf8(PyUnicode_AsUTF8(key));
          map[qkey] = PyObjectToQVariant(value);
        }
        Py_DECREF(dict);
        return map;
      }
      Py_XDECREF(dict);
    }
  }

  else if (obj == Py_None) {
    return {};
  }

  return QString("<unsupported type>");
}

QMessage PyDataclassToQMessage(PyObject *obj) {
  QMessage m;

  PyObject *dict = PyObject_GetAttrString(obj, "__dict__");
  if (!dict || !PyDict_Check(dict))
    return m;

  auto getStr = [&](const char *name) -> QString {
    PyObject *val = PyDict_GetItemString(dict, name);
    return (val && PyUnicode_Check(val)) ? QString::fromUtf8(PyUnicode_AsUTF8(val)) : QString();
  };

  auto getBool = [&](const char *name) -> bool {
    PyObject *val = PyDict_GetItemString(dict, name);
    return val && PyObject_IsTrue(val);
  };

  auto getBytes = [&](const char *name) -> QByteArray {
    PyObject *val = PyDict_GetItemString(dict, name);
    if (val && PyBytes_Check(val))
      return QByteArray(PyBytes_AsString(val), PyBytes_Size(val));
    return {};
  };

  m.id          = getBytes("id");
  m.nick        = getBytes("nick");
  m.user        = getBytes("user");
  m.host        = getBytes("host");
  m.text        = getStr("text").toUtf8();
  m.raw         = getBytes("raw");
  m.from_server = getBool("from_server");

  PyObject *tagsObj = PyDict_GetItemString(dict, "tags");
  if (tagsObj)
    m.tags = PyObjectToQVariant(tagsObj).toMap();

  PyObject *targetsObj = PyDict_GetItemString(dict, "targets");
  if (targetsObj)
    m.targets = PyObjectToQVariant(targetsObj).toStringList();

  PyObject *accObj = PyDict_GetItemString(dict, "account");
  if (accObj)
    m.account = PyObjectToQVariant(accObj);

  Py_XDECREF(dict);
  return m;
}

QAuthUserResult PyDataclassToQAuthUserResult(PyObject *obj) {
  QAuthUserResult r;

  PyObject *dict = PyObject_GetAttrString(obj, "__dict__");
  if (!dict || !PyDict_Check(dict))
    return r;

  PyObject *res = PyDict_GetItemString(dict, "result");
  if (res)
    r.result = PyObject_IsTrue(res);

  PyObject *reason = PyDict_GetItemString(dict, "reason");
  if (reason && PyUnicode_Check(reason))
    r.reason = PyUnicode_AsUTF8(reason);

  Py_XDECREF(dict);
  return r;
}

