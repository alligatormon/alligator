#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <uv.h>
#include "main.h"
//#include <stdlib.h>
//#include <sys/types.h>
//#include <sys/stat.h>
//#include <time.h>
//#include <unistd.h>
//#include "common/mkdirp.h"

size_t get_rec_dir (char *str, size_t len, uint64_t num, int *fin)
{
	char *cursor = str, *cursor_tmp;
	uint64_t i;
	for ( i=0; i<num; i++ )
	{
		cursor_tmp = cursor;
		cursor = strstr(cursor, "/");
		if (cursor == cursor_tmp)
		{
			i--;
			cursor+=1;
			continue;
		}
		if (cursor == NULL)
		{
			*fin=1;
			return len;
		}
		cursor+=1;
	}
	return cursor-str;
}

char * get_dir (char *str, uint64_t num, int *fin)
{
	size_t len = strlen(str);
	char *dir = malloc(len+1);
	size_t rc = get_rec_dir(str, len, num, fin);
	strlcpy(dir,str,rc+1);
	return dir;
}

void mkdir_cb(uv_fs_t* req)
{
	extern aconf *ac;
	int result = req->result;

	if ((ac->log_level > 1) && (result == -1))
		fprintf(stderr, "Error at creating directory: %s\n", (char*)req->data);

	if (ac->log_level > 2)
		printf("Successfully created directory: %s\n", (char*)req->data);

	uv_fs_req_cleanup(req);
	free(req->data);
	free(req);
}

int mkdirp(char *dir)
{
	extern aconf *ac;
	uint64_t i;
	int fin=0;
	char *r_dir;
	for ( i=0; fin!=1; i++ )
	{
		r_dir = get_dir (dir, i, &fin);
		uv_fs_t *mkdir_req = malloc(sizeof(*mkdir_req));
		mkdir_req->data = r_dir;
		//printf("creating %s\n", r_dir);
		uv_fs_mkdir(ac->loop, mkdir_req, r_dir, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, mkdir_cb);
	}
	return 0;
}
