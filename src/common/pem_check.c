#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "mbedtls/x509_crt.h"
#include "common/rtime.h"
#include "lang/lang.h"
#include "common/pem_check.h"
#include "common/lcrypto.h"
#include "scheduler/type.h"
#include "modules/modules.h"
#include "events/fs_read.h"
#include "scheduler/type.h"
#include "main.h"

int x509_fs_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((x509_fs_t*)obj)->name;
	return strcmp(s1, s2);
}

uint64_t get_timestamp_from_mbedtls(mbedtls_x509_time mbed_tm)
{
	struct tm gmtime_time;
	memset(&gmtime_time, 0, sizeof(gmtime_time));
	gmtime_time.tm_year = mbed_tm.year - 1900;
	gmtime_time.tm_mon = mbed_tm.mon - 1;
	gmtime_time.tm_mday = mbed_tm.day;
	gmtime_time.tm_hour = mbed_tm.hour;
	gmtime_time.tm_min = mbed_tm.min;
	gmtime_time.tm_sec = mbed_tm.sec;

	char buf[255];
	uint64_t timestamp;
	strftime(buf, 255, "%s", &gmtime_time);
	timestamp = strtoull(buf, NULL, 10);
	

	return timestamp;
}

char *read_file(char *name)
{
	FILE *fd = fopen(name, "r");
	if (!fd)
		return 0;

	char *pem_cert = malloc(2048);
	size_t rc = fread(pem_cert, 1, 2048, fd);
	fclose(fd);
	if (!rc)
	{
		free(pem_cert);
		return NULL;
	}

	return pem_cert;
}

void parse_cert_info(const mbedtls_x509_crt *cert_ctx, char *cert, char *host)
{
	if (!cert_ctx)
		return;

	extern aconf *ac;

	char dn_subject[1000];
	int dn_subject_size = mbedtls_x509_dn_gets( dn_subject, 1000, &cert_ctx->subject );
	char country_name[1000];
	char county[1000];
	char organization_name[1000];
	char organization_unit[1000];
	char common_name[1000];
	alligator_ht *lbl = calloc(1, sizeof(*lbl));
	alligator_ht_init(lbl);
	labels_hash_insert_nocache(lbl, "cert", cert);
	if (host)
		labels_hash_insert_nocache(lbl, "host", host);

	for(int i=0; i<dn_subject_size;)
	{
		if (!strncmp(dn_subject+i, "C=", 2))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", ");
			strlcpy(country_name, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, country=%s\n", cert, country_name);
			labels_hash_insert_nocache(lbl, "country", country_name);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "ST=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", ");
			strlcpy(county, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, county=%s\n", cert, county);
			labels_hash_insert_nocache(lbl, "county", county);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "O=", 2))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", ");
			strlcpy(organization_name, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, organization_name=%s\n", cert, organization_name);
			labels_hash_insert_nocache(lbl, "organization_name", organization_name);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "OU=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", ");
			strlcpy(organization_unit, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, organization_unit=%s\n", cert, organization_unit);
			labels_hash_insert_nocache(lbl, "organization_unit", organization_unit);
			i += size;
		}
		else if (!strncmp(dn_subject+i, "CN=", 3))
		{
			i += strcspn(dn_subject+i, "= ");
			i += strspn(dn_subject+i, "= ");
			int size = strcspn(dn_subject+i, ", ");
			strlcpy(common_name, dn_subject+i, size+1);
			if (ac->log_level > 2)
				printf("cert: %s, common_name=%s\n", cert, common_name);
			labels_hash_insert_nocache(lbl, "common_name", common_name);
			i += size;
		}
		i += strcspn(dn_subject+i, ",");
		i += strspn(dn_subject+i, ", \t");
	}
	
	char dn_issuer[1000];
	mbedtls_x509_dn_gets(dn_issuer, 1000, &cert_ctx->issuer);
	labels_hash_insert_nocache(lbl, "issuer", dn_issuer);
	if (ac->log_level > 2)
		printf("cert: %s, issuer: %s\n", cert, dn_issuer);

	char serial[100];
	mbedtls_x509_serial_gets(serial, 100, &cert_ctx->serial);
	if (ac->log_level > 2)
		printf("cert: %s, serial: %s\n", cert, serial);
	labels_hash_insert_nocache(lbl, "serial", serial);

	int64_t valid_from = get_timestamp_from_mbedtls(cert_ctx->valid_from);
	int64_t valid_to = get_timestamp_from_mbedtls(cert_ctx->valid_to);
	r_time now = setrtime();

	int64_t expdays =  (valid_to-now.sec)/86400; 
	if (ac->log_level > 2)
	{
		printf("cert: %s, certsubject: %s\n", cert, dn_subject);
		printf("cert: %s, complete for: %u.\n", cert, now.sec);
		printf("cert: %s, valid from: %"d64".\n", cert, valid_from);
		printf("cert: %s, %"d64" exp\n", cert, expdays);
		printf("cert: %s, version: %d\n", cert, cert_ctx->version);
	}
	alligator_ht *notafter_lbl = labels_dup(lbl);
	alligator_ht *expiredays_lbl = labels_dup(lbl);
	metric_add("x509_cert_not_before", lbl, &valid_from, DATATYPE_INT, NULL);
	metric_add("x509_cert_not_after", notafter_lbl, &valid_to, DATATYPE_INT, NULL);
	metric_add("x509_cert_expire_days", expiredays_lbl, &expdays, DATATYPE_INT, NULL);
}

void pem_check_cert(char *pem_cert, size_t cert_size, void *data, char *filename)
{
	mbedtls_x509_crt cert;
	mbedtls_x509_crt_init(&cert);

	int ret = mbedtls_x509_crt_parse(&cert, (const unsigned char *)pem_cert, cert_size+2);
	if (ret)
	{
		if (ac->log_level > 2)
			printf("error parse certificate %s\n", filename);
		if (ac->log_level > 10)
			printf("error parse certificate %s: '%s' with size %zu/%zu\n", filename, pem_cert, strlen(pem_cert), cert_size+2);
		free(data);
		free(filename);
		return;
	}

	parse_cert_info(&cert, filename, NULL);

	free(filename);
	mbedtls_x509_crt_free(&cert);
	free(data);
}

void fs_cert_check(char *name, char *fname, char *match, char *password, uint8_t type)
{
	void *func = pem_check_cert;
	if (type == X509_TYPE_PFX)
		func = libcrypto_check_cert;
	if (strstr(fname, match))
		read_from_file(fname, 0, func, password);
		//read_from_file(fname, 0, pem_check_cert, NULL);
	else
	{
		free(fname);
	}
}

//int min(int a, int b) { return (a < b)? a : b;  }
int tls_fs_dir_read(char *name, char *path, char *match, char *password, uint8_t type)
{
	uv_fs_t readdir_req;

	uv_fs_opendir(NULL, &readdir_req, path, NULL);
	if (ac->log_level > 0)
		printf("tls open dir: %s, status: %p\n", path, readdir_req.ptr);

	if (!readdir_req.ptr)
		return 0;

	uv_dirent_t dirents[1024];
	uv_dir_t* rdir = readdir_req.ptr;
	rdir->dirents = dirents;
	rdir->nentries = 1024;//min(1024,chunklen);

	int acc = 0;

	char fullname[1024];
	strcpy(fullname, path);
	char * filebase = fullname+strlen(path)+1;
	*(filebase-1)='/';

	for(;;)
	{
		int r = uv_fs_readdir(uv_default_loop(), &readdir_req, readdir_req.ptr, NULL);
		if (r <= 0)
			break;

		for (int i=0; i<r; i++)
		{
			if (dirents[i].type == UV_DIRENT_DIR)
			{
				strcpy(filebase, dirents[i].name);
				acc += tls_fs_dir_read(name, fullname, match, password, type);
			}
			else if (dirents[i].type == UV_DIRENT_FILE || dirents[i].type == UV_DIRENT_LINK)
			{
				//printf("\tpath '%s', dirents[i].name '%s'\n", path, dirents[i].name);
				acc += 1;
				char *filename = malloc(1024);
				snprintf(filename, 1023, "%s/%s", path, dirents[i].name);
				fs_cert_check(name, filename, match, password, type);
			}
			free((void*)dirents[i].name);
		}
	}

	uv_fs_closedir(NULL, &readdir_req, readdir_req.ptr, NULL);
	return acc;
}

void tls_fs_recurse(void *arg)
{
	if (!arg)
		return;

	x509_fs_t *tls_fs = arg;

	tls_fs_dir_read(tls_fs->name, tls_fs->path, tls_fs->match, tls_fs->password, tls_fs->type);
}

void for_tls_fs_recurse(void *arg)
{
	x509_fs_t *tls_fs = arg;
	if (tls_fs->period)
		return;

	tls_fs_recurse(arg);
}

void for_tls_fs_recurse_repeat_period(uv_timer_t *timer)
{
	x509_fs_t *tls_fs = timer->data;
	if (!tls_fs->period)
		return;

	uv_timer_set_repeat(tls_fs->period_timer, tls_fs->period);

	tls_fs_recurse((void*)tls_fs);
}

static void tls_fs_crawl(uv_timer_t* handle) {
	(void)handle;
	alligator_ht_foreach(ac->fs_x509, for_tls_fs_recurse);
}

void tls_fs_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->tls_fs_timer);
	uv_timer_start(&ac->tls_fs_timer, tls_fs_crawl, ac->tls_fs_startup, ac->tls_fs_repeat);
}

void tls_fs_push(char *name, char *path, char *match, char *password, char *type, uint64_t period)
{
	x509_fs_t *tls_fs = calloc(1, sizeof(*tls_fs));
	tls_fs->name = strdup(name);
	tls_fs->path = strdup(path);
	tls_fs->match = strdup(match);
	tls_fs->period = period;

	if (password)
		tls_fs->password = strdup(password);

	if (type && !strcmp(type, "pfx"))
		tls_fs->type = X509_TYPE_PFX;

	alligator_ht_insert(ac->fs_x509, &(tls_fs->node), tls_fs, tommy_strhash_u32(0, tls_fs->name));

	if (tls_fs->period) {
		tls_fs->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		tls_fs->period_timer->data = tls_fs;
		uv_timer_init(ac->loop, tls_fs->period_timer);
		uv_timer_start(tls_fs->period_timer, for_tls_fs_recurse_repeat_period, tls_fs->period, tls_fs->period);
	}
}

void tls_fs_del_node(x509_fs_t *tls_fs)
{
	free(tls_fs->name);
	free(tls_fs->path);
	free(tls_fs->match);
	free(tls_fs->password);
	free(tls_fs);
}

void tls_fs_del(char *name)
{
	x509_fs_t *tls_fs = alligator_ht_search(ac->fs_x509, x509_fs_compare, name, tommy_strhash_u32(0, name));
	if (tls_fs)
	{
		alligator_ht_remove_existing(ac->fs_x509, &(tls_fs->node));
		tls_fs_del_node(tls_fs);
	}
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

void jks_push(char *name, char *path, char *match, char *password, char *passtr, uint64_t period)
{
	if (ac->log_level > 0)
		printf("run jks_push with name %s, path %s, match %s, and password/passtr %p/%p\n", name, path, match, password, passtr);

	if (!password && !passtr)
	{
		printf("no set password for jks: %s\n", name);
		return;
	}

	lang_options *lo = calloc(1, sizeof(*lo));
	lo->key = strdup(name);
	lo->lang = "so";
	lo->module = "parseJks";
	lo->method = "alligator_call";
	lo->hidden_arg = 1;

	if (!passtr)
	{
		size_t len = strlen(path) + strlen(match) + strlen(password) + 3;
		passtr = malloc (len + 1);
		snprintf(passtr, len, "%s %s %s", path, match, password);
	}

	lo->arg = passtr;

	lang_push_options(lo);
	scheduler_node* sn = scheduler_get(name);
	if (!sn) {
		sn = calloc(1, sizeof(*sn));
		sn->name = strdup(name);
		sn->period = period;
		sn->lang = strdup(name);
		uint32_t hash = tommy_strhash_u32(0, sn->name);
		alligator_ht_insert(ac->scheduler, &(sn->node), sn, hash);
		scheduler_start(sn);
	}

	const char *module_key = "parseJks";
	module_t *module = alligator_ht_search(ac->modules, module_compare, module_key, tommy_strhash_u32(0, module_key));
	if (!module)
	{
		module_t *module = calloc(1, sizeof(*module));
		module->key = strdup(module_key);
		module->path = strdup("/var/lib/alligator/parseJks.so");
		alligator_ht_insert(ac->modules, &(module->node), module, tommy_strhash_u32(0, module->key));
	}
}

void jks_del(char *name)
{
	lang_delete(name);
}
