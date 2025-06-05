#pragma once
#define DRIVEDB_NGRAM_LEN 3
#include "dstructures/ht.h"
#include "dstructures/ngram/ngram.h"

typedef struct drivedb_struct {
	tommy_node node;
	int key;
	char *identificator;
} drivedb_struct;

typedef struct drive_settings {
	const char *modelfamily;
	const char *modelregexp;
	const char *firmwareregexp;
	const char *warningmsg;
	const char *presets;
} drive_settings;

typedef struct drive_settings_extended {
	drive_settings ds;
	alligator_ht *presets_identificators;
	char device[255];
} drive_settings_extended;


drive_settings_extended *drivedb_search(ngram_index_t *ngram_table, char *query);
char* drivedb_get_identificator(drive_settings_extended *dse, int id);
ngram_index_t* drivedb_load();
