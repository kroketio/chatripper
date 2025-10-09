#pragma once
#include "Python.h"
#include <QObject>
#include <QMetaType>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include "module.h"

// QVariant -> PyObject* (recursive)
PyObject* QVariantToPyObject(const QVariant &var);

// PyObject* -> QVariant (recursive)
QVariant PyObjectToQVariant(PyObject *obj);
//
PyObject* convertListToPy(const QVariantList& list, PyObject* elemType);
PyObject* convertMapToPy(const QVariantMap& map, PyObject* valueType);
// dataclasses
PyObject* convertVariantToPyAccordingToType(const QVariant& value, PyObject* pyType);
QVariant convertPyObjectToVariant(PyObject* obj);
