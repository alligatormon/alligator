#include "x509/type.h"
#include "lang/type.h"
#include "common/logs.h"
#include "scheduler/type.h"
#include "main.h"
#include <string.h>

int x509_fs_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((x509_fs_t*)obj)->name;
	return strcmp(s1, s2);
}


void tls_fs_del_node(x509_fs_t *tls_fs)
{
	free(tls_fs->name);
	free(tls_fs->path);
	free(tls_fs->match);
	free(tls_fs->password);
	free(tls_fs);
}

int tls_fs_del(char *name)
{
	x509_fs_t *tls_fs = alligator_ht_search(ac->fs_x509, x509_fs_compare, name, tommy_strhash_u32(0, name));
	if (tls_fs)
	{
		alligator_ht_remove_existing(ac->fs_x509, &(tls_fs->node));
		tls_fs_del_node(tls_fs);
	}
	return 1;
}

void tls_fs_free_foreach(void *arg)
{
	x509_fs_t *tls_fs = arg;
	tls_fs_del_node(tls_fs);
}

void tls_fs_free()
{
	alligator_ht_foreach(ac->fs_x509, tls_fs_free_foreach);
}

int jks_del(char *name)
{
	lang_delete_key(name);
	scheduler_del(name);
	return 1;
}

int x509_del(json_t *x509) {
	json_t *jname = json_object_get(x509, "name");
	if (!jname) {
		glog(L_INFO, "not specified param 'name' in x509 context while deleting\n");
		return 0;
	}
	char *name = (char*)json_string_value(jname);


	json_t *jtype = json_object_get(x509, "type");
	char *type = (char*)json_string_value(jtype);

	if (!strcmp(type, "jks"))
		return jks_del(name);
	else
		return tls_fs_del(name);
}
