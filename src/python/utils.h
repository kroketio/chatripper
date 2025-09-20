#pragma once
#include "Python.h"
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

// QVariant -> PyObject* (recursive)
PyObject* QVariantToPyObject(const QVariant &var);

// PyObject* -> QVariant (recursive)
QVariant PyObjectToQVariant(PyObject *obj);