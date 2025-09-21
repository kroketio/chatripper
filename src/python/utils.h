#pragma once
#include "Python.h"
#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include "module.h"

// QVariant -> PyObject* (recursive)
PyObject* QVariantToPyObject(const QVariant &var);

// PyObject* -> QVariant (recursive)
QVariant PyObjectToQVariant(PyObject *obj);

// dataclasses
QMessage PyDataclassToQMessage(PyObject *obj);
QAuthUserResult PyDataclassToQAuthUserResult(PyObject *obj);