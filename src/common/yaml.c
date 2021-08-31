#include <stdlib.h>
#include "common/yaml.h"

#ifdef __linux__
#include <libfyaml.h>
#endif
#ifdef __FreeBSD__
#define FYECF_MODE_JSON 1
#define FY_NT 2
struct fy_document {
	void *id;
};

struct fy_document *fy_document_build_from_file(struct fy_document *fyd, char* path) { return NULL; }
struct fy_document *fy_document_build_from_string(struct fy_document *fyd, char* str, int mode) { return NULL; }
char *fy_emit_document_to_string(struct fy_document *fyd, int mode) { return NULL; }
int fy_emit_document_to_file(struct fy_document *fyd, int mode, char *path) { return 0; }
void fy_document_destroy(struct fy_document *fyd) {};

#endif

char *yaml_file_to_json_str(char *path)
{
	struct fy_document *fyd = fy_document_build_from_file(NULL, path);
	char *buf = fy_emit_document_to_string(fyd, FYECF_MODE_JSON);
	//printf("buf %s\n", buf);
	fy_document_destroy(fyd);
	return buf;
}

char *yaml_str_to_json_str(char *yaml)
{
	struct fy_document *fyd = fy_document_build_from_string(NULL, yaml, FY_NT);
	char *buf = fy_emit_document_to_string(fyd, FYECF_MODE_JSON);
	//printf("buf %s\n", buf);
	fy_document_destroy(fyd);
	return buf;
}

int yaml_file_to_json_file(char *path_to_yaml, char *path_to_json)
{
	struct fy_document *fyd = fy_document_build_from_file(NULL, path_to_yaml);
	int rc = fy_emit_document_to_file(fyd, FYECF_MODE_JSON, path_to_json);
	fy_document_destroy(fyd);
	return rc;
}

int yaml_str_to_json_file(char *yaml, char *path_to_json)
{
	struct fy_document *fyd = fy_document_build_from_string(NULL, yaml, FY_NT);
	int rc = fy_emit_document_to_file(fyd, FYECF_MODE_JSON, path_to_json);
	fy_document_destroy(fyd);
	return rc;
}
