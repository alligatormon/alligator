#include "main.h"
#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include "modules/modules.h"
extern aconf* ac;

char* java_run(char *optionString, char* className, char *method, char *arg)
{
	JavaVMOption options[3];
	JavaVM *jvm;
	JavaVMInitArgs vm_args;
	long status;
	jclass cls;
	jmethodID mid;

	options[0].optionString = optionString;
	options[1].optionString = "-Xmx100m";
	options[2].optionString = "-Xms100m";
	memset(&vm_args, 0, sizeof(vm_args));
	vm_args.version = JNI_VERSION_1_2;
	vm_args.nOptions = 3;
	vm_args.options = options;

	if (!ac->create_jvm)
	{
		module_t *libjvm = tommy_hashdyn_search(ac->modules, module_compare, "jvm", tommy_strhash_u32(0, "jvm"));
		if (!libjvm)
		{
			if (ac->log_level > 0)
				printf("no module with key jvm\n");
			return NULL;
		}

		if (ac->log_level > 0)
			printf("initialize JVM from library: %s\n", libjvm->path);

		*(void**)(&ac->create_jvm) = module_load(libjvm->path, "JNI_CreateJavaVM", &ac->libjvm_handle);
		if (!ac->create_jvm)
		{
			if (ac->log_level > 0)
				printf("error load module: %s, with function: JNI_CreateJavaVM\n", libjvm->path);
			return NULL;
		}

		status = ac->create_jvm(&jvm, (void**)&ac->env, &vm_args);
		if (status == JNI_ERR)
		{
			if (ac->log_level > 0)
				printf("status == JNI_ERR: %ld\n", status);
			return NULL;
		}
	}

	cls = (*ac->env)->FindClass(ac->env, className);
	if (!cls) {
		if (ac->log_level > 0)
		{
			printf("Cannot find class with name: %s\n", className);
			(*ac->env)->ExceptionDescribe(ac->env);
		}
		return NULL;
	}

	//mid = (*ac->env)->GetMethodID(ac->env, cls, method, "()Ljava/lang/String;");
	mid = (*ac->env)->GetMethodID(ac->env, cls, method, "(Ljava/lang/String;)Ljava/lang/String;");
	jobject classifierObj = (*ac->env)->NewObject(ac->env, cls, mid);

	char *ptr = NULL;
	if (mid)
	{
		jstring js = (*ac->env)->NewStringUTF(ac->env, arg);
		jstring rv = (*ac->env)->CallObjectMethod(ac->env, classifierObj, mid, js);

		ptr = (char*)(*ac->env)->GetStringUTFChars(ac->env, rv, 0);
		printf("Result of '%s': '%s'\n", method, ptr);
		(*ac->env)->DeleteLocalRef(ac->env, rv);
		//free((char*)ptr);
	}
	(*ac->env)->DeleteLocalRef(ac->env, classifierObj);

	return ptr;
}
