#pragma once
#include <Python.h>

typedef struct python_lib {
	void (*Py_Initialize)();
	uv_lib_t *Py_Initialize_lib;

	PyObject* (*PyUnicode_DecodeFSDefault)(char*);
	uv_lib_t *PyUnicode_DecodeFSDefault_lib;

	PyObject* (*PyImport_Import)(PyObject*);
	uv_lib_t *PyImport_Import_lib;

	PyObject* (*PyObject_GetAttrString)(PyObject*, char*);
	uv_lib_t *PyObject_GetAttrString_lib;

	int (*PyCallable_Check)(PyObject*);
	uv_lib_t *PyCallable_Check_lib;

	int (*PyErr_Occurred)();
	uv_lib_t *PyErr_Occurred_lib;

	void (*PyErr_Print)();
	uv_lib_t *PyErr_Print_lib;

	PyObject* (*PyTuple_New)(int);
	uv_lib_t *PyTuple_New_lib;

	PyObject* (*PyUnicode_FromString)(char*);
	uv_lib_t *PyUnicode_FromString_lib;

	PyObject* (*PyObject_Repr)(PyObject*);
	uv_lib_t *PyObject_Repr_lib;

	int (*Py_FinalizeEx)();
	uv_lib_t *Py_FinalizeEx_lib;

	void (*PyTuple_SetItem)(PyObject*, int, PyObject*);
	uv_lib_t *PyTuple_SetItem_lib;

	PyObject* (*PyObject_CallObject)(PyObject*, PyObject*);
	uv_lib_t *PyObject_CallObject_lib;

	PyObject* (*PyUnicode_AsEncodedString)(PyObject*, char*, char*);
	uv_lib_t *PyUnicode_AsEncodedString_lib;

	PyObject* (*PySys_GetObject)(char*);
	uv_lib_t *PySys_GetObject_lib;

	void (*PyList_Append)(PyObject*, PyObject*);
	uv_lib_t *PyList_Append_lib;
} python_lib;

