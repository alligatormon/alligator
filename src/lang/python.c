#define PY_SSIZE_T_CLEAN
#include "modules/modules.h"
#include "lang/python.h"
#include "lang/lang.h"
#include "main.h"

extern aconf *ac;
void _Py_Dealloc(PyObject *op);

void python_free(python_lib *pylib)
{
	if (!pylib)
		return;

	if (pylib->Py_Initialize)
		free(pylib->Py_Initialize);

	if (pylib->PyUnicode_DecodeFSDefault)
		free(pylib->PyUnicode_DecodeFSDefault);

	if (pylib->PyImport_Import)
		free(pylib->PyImport_Import);

	if (pylib->PyObject_GetAttrString)
		free(pylib->PyObject_GetAttrString);

	if (pylib->PyCallable_Check)
		free(pylib->PyCallable_Check);

	if (pylib->PyErr_Occurred)
		free(pylib->PyErr_Occurred);

	if (pylib->PyErr_Print)
		free(pylib->PyErr_Print);

	if (pylib->PyTuple_New)
		free(pylib->PyTuple_New);

	if (pylib->PyUnicode_FromString)
		free(pylib->PyUnicode_FromString);

	if (pylib->PyObject_Repr)
		free(pylib->PyObject_Repr);

	if (pylib->Py_FinalizeEx)
		free(pylib->Py_FinalizeEx);

	if (pylib->PyTuple_SetItem)
		free(pylib->PyTuple_SetItem);

	if (pylib->PyObject_CallObject)
		free(pylib->PyObject_CallObject);

	if (pylib->PyUnicode_AsEncodedString)
		free(pylib->PyUnicode_AsEncodedString);

	if (pylib->PySys_GetObject)
		free(pylib->PySys_GetObject);

	if (pylib->PyList_Append)
		free(pylib->PyList_Append);

	free(pylib);
}

python_lib *python_init()
{
	module_t *libpython = alligator_ht_search(ac->modules, module_compare, "python", tommy_strhash_u32(0, "python"));
	if (!libpython)
	{
		printf("No defined python library in configuration\n");
		return NULL;
	}


	python_lib *pylib = calloc(1, sizeof(*pylib));

	pylib->Py_Initialize = module_load(libpython->path, "Py_Initialize", &pylib->Py_Initialize_lib);
	if (!pylib->Py_Initialize)
	{
		printf("Cannot get Py_Initialize from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyUnicode_DecodeFSDefault = (void*)module_load(libpython->path, "PyUnicode_DecodeFSDefault", &pylib->PyUnicode_DecodeFSDefault_lib);
	if (!pylib->PyUnicode_DecodeFSDefault)
	{
		printf("Cannot get PyUnicode_DecodeFSDefault from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyImport_Import = (void*)module_load(libpython->path, "PyImport_Import", &pylib->PyImport_Import_lib);
	if (!pylib->PyImport_Import)
	{
		printf("Cannot get PyImport_Import from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyObject_GetAttrString = (void*)module_load(libpython->path, "PyObject_GetAttrString", &pylib->PyObject_GetAttrString_lib);
	if (!pylib->PyObject_GetAttrString)
	{
		printf("Cannot get PyObject_GetAttrString from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyCallable_Check = (void*)module_load(libpython->path, "PyCallable_Check", &pylib->PyCallable_Check_lib);
	if (!pylib->PyCallable_Check)
	{
		printf("Cannot get PyCallable_Check from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyErr_Occurred = (void*)module_load(libpython->path, "PyErr_Occurred", &pylib->PyErr_Occurred_lib);
	if (!pylib->PyErr_Occurred)
	{
		printf("Cannot get PyErr_Occurred from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyErr_Print = (void*)module_load(libpython->path, "PyErr_Print", &pylib->PyErr_Print_lib);
	if (!pylib->PyErr_Print)
	{
		printf("Cannot get PyErr_Print from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyTuple_New = (void*)module_load(libpython->path, "PyTuple_New", &pylib->PyTuple_New_lib);
	if (!pylib->PyTuple_New)
	{
		printf("Cannot get PyTuple_New from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyUnicode_FromString = (void*)module_load(libpython->path, "PyUnicode_FromString", &pylib->PyUnicode_FromString_lib);
	if (!pylib->PyUnicode_FromString)
	{
		printf("Cannot get PyUnicode_FromString from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyObject_Repr = (void*)module_load(libpython->path, "PyObject_Repr", &pylib->PyObject_Repr_lib);
	if (!pylib->PyObject_Repr)
	{
		printf("Cannot get PyObject_Repr from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->Py_FinalizeEx = (void*)module_load(libpython->path, "Py_FinalizeEx", &pylib->Py_FinalizeEx_lib);
	if (!pylib->Py_FinalizeEx)
	{
		printf("Cannot get Py_FinalizeEx from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyTuple_SetItem = (void*)module_load(libpython->path, "PyTuple_SetItem", &pylib->PyTuple_SetItem_lib);
	if (!pylib->PyTuple_SetItem)
	{
		printf("Cannot get PyTuple_SetItem from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyObject_CallObject = (void*)module_load(libpython->path, "PyObject_CallObject", &pylib->PyObject_CallObject_lib);
	if (!pylib->PyObject_CallObject)
	{
		printf("Cannot get PyObject_CallObject from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyUnicode_AsEncodedString = (void*)module_load(libpython->path, "PyUnicode_AsEncodedString", &pylib->PyUnicode_AsEncodedString_lib);
	if (!pylib->PyUnicode_AsEncodedString)
	{
		printf("Cannot get PyUnicode_AsEncodedString from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PySys_GetObject = (void*)module_load(libpython->path, "PySys_GetObject", &pylib->PySys_GetObject_lib);
	if (!pylib->PySys_GetObject)
	{
		printf("Cannot get PySys_GetObject from libpython\n");
		python_free(pylib);
		return NULL;
	}

	pylib->PyList_Append = (void*)module_load(libpython->path, "PyList_Append", &pylib->PyList_Append_lib);
	if (!pylib->PyList_Append)
	{
		printf("Cannot get PyList_Append from libpython\n");
		python_free(pylib);
		return NULL;
	}

	return pylib;
}

char* python_run(lang_options *lo, char* code, char *file, char *arg, char *path, string* metrics, string *conf)
{
	PyObject *pName, *pModule, *pFunc;
	PyObject *pArgs, *pValue;
	char *ret = NULL;

	if (!ac->pylib)
		ac->pylib = python_init();

	if (!path)
		path = "/var/lib/alligator/";

	python_lib *pylib = ac->pylib;
	if (!pylib)
	{
		if (lo->log_level)
			printf("not load python library\n");
		return ret;
	}

	pylib->Py_Initialize();
	pName = pylib->PyUnicode_DecodeFSDefault(file);

	PyObject* sysPath = pylib->PySys_GetObject((char*)"path");
	PyObject* programName = pylib->PyUnicode_FromString(path);
	pylib->PyList_Append(sysPath, programName);
	Py_DECREF(programName);

	pModule = pylib->PyImport_Import(pName);
	Py_DECREF(pName);

	if (pModule != NULL)
	{
		pFunc = pylib->PyObject_GetAttrString(pModule, lo->method);

		if (pFunc && pylib->PyCallable_Check(pFunc))
		{
			pArgs = pylib->PyTuple_New(3);
			if (!arg)
				arg = "";
			char *metrics_str = metrics ? metrics->s : "";
			char *conf_str = conf ? conf->s : "";

			// arg
			pValue = pylib->PyUnicode_FromString(arg);
			if (!pValue)
			{
				Py_DECREF(pArgs);
				Py_DECREF(pModule);
				if (lo->log_level)
					fprintf(stderr, "Cannot convert argument\n");
				return NULL;
			}
			pylib->PyTuple_SetItem(pArgs, 0, pValue);

			// metrics
			pValue = pylib->PyUnicode_FromString(metrics_str);
			if (!pValue)
			{
				Py_DECREF(pArgs);
				Py_DECREF(pModule);
				if (lo->log_level)
					fprintf(stderr, "Cannot convert input metrics\n");
				return NULL;
			}
			pylib->PyTuple_SetItem(pArgs, 1, pValue);

			// conf
			pValue = pylib->PyUnicode_FromString(conf_str);
			if (!pValue)
			{
				Py_DECREF(pArgs);
				Py_DECREF(pModule);
				if (lo->log_level)
					fprintf(stderr, "Cannot convert input conf\n");
				return NULL;
			}
			pylib->PyTuple_SetItem(pArgs, 2, pValue);

			pValue = pylib->PyObject_CallObject(pFunc, pArgs);
			Py_DECREF(pArgs);
			if (pValue != NULL)
			{
				PyObject* pResultRepr = pylib->PyObject_Repr(pValue);
				//ret = strdup(PyBytes_AS_STRING(pylib->PyUnicode_AsEncodedString(pResultRepr, "utf-8", "ERROR")));
				PyObject *res_obj = pylib->PyUnicode_AsEncodedString(pResultRepr, "utf-8", "ERROR");
				uint64_t size_bytes = PyBytes_GET_SIZE(res_obj) - 2;
				char *ret_str = PyBytes_AS_STRING(res_obj);
				if (!ret_str)
					printf("Python lang key '%s' return error: '%s'\n", lo->key, ret);
				else
					ret = strndup(ret_str + 1, size_bytes);

				if (lo->log_level)
					printf("Python lang key '%s' return: '%s'\n", lo->key, ret);

				Py_XDECREF(pResultRepr);
				Py_DECREF(pValue);
			}
			else
			{
				Py_DECREF(pFunc);
				Py_DECREF(pModule);
				pylib->PyErr_Print();
				if (lo->log_level)
					fprintf(stderr,"Call failed\n");
				return NULL;
			}
		}
		else
		{
			if (pylib->PyErr_Occurred())
				pylib->PyErr_Print();

			fprintf(stderr, "Cannot find function \"%s\"\n", "alligator_run");
		}
		Py_XDECREF(pFunc);
		Py_DECREF(pModule);
	}
	else
	{
		pylib->PyErr_Print();
		fprintf(stderr, "Failed to load \"%s\"\n", file);
		return NULL;
	}

	pylib->Py_FinalizeEx();

	return ret;
}
