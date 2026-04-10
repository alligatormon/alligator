#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include "system/common.h"
extern aconf *ac;

void system_test(char *binary) {
	system_initialize();
    ac->system_procfs = malloc(PATH_MAX + 1);
    ac->system_sysfs = malloc(PATH_MAX + 1);
    ac->system_rundir = malloc(PATH_MAX + 1);
    ac->system_usrdir = malloc(PATH_MAX + 1);
    ac->system_etcdir = malloc(PATH_MAX + 1);

    char *bin_copy = strdup(binary);
    if (!bin_copy) {
        free(ac->system_procfs);
        free(ac->system_sysfs);
        free(ac->system_rundir);
        free(ac->system_usrdir);
        free(ac->system_etcdir);
        ac->system_procfs = ac->system_sysfs = ac->system_rundir = ac->system_usrdir = ac->system_etcdir = NULL;
        return;
    }
    char *pathbin = dirname(bin_copy);
    char *mockpath = malloc(PATH_MAX + 1);
    char *cwd = malloc(PATH_MAX + 1);
    if (!mockpath || !cwd) {
        free(mockpath);
        free(cwd);
        free(bin_copy);
        free(ac->system_procfs);
        free(ac->system_sysfs);
        free(ac->system_rundir);
        free(ac->system_usrdir);
        free(ac->system_etcdir);
        ac->system_procfs = ac->system_sysfs = ac->system_rundir = ac->system_usrdir = ac->system_etcdir = NULL;
        return;
    }
    if (*pathbin == '/') {
        snprintf(mockpath, PATH_MAX, "%s/../tests/mock/linux/", pathbin);
    }
    else {
        if (!getcwd(cwd, PATH_MAX + 1))
            cwd[0] = '\0';
        snprintf(mockpath, PATH_MAX, "%s/%s/../tests/mock/linux/", cwd, pathbin);
    }

    snprintf(ac->system_procfs, PATH_MAX, "%s/proc", mockpath);
    snprintf(ac->system_sysfs, PATH_MAX, "%s/sys",   mockpath);
    snprintf(ac->system_rundir, PATH_MAX, "%s/run",  mockpath);
    snprintf(ac->system_usrdir, PATH_MAX, "%s/usr",  mockpath);
    snprintf(ac->system_etcdir, PATH_MAX, "%s/etc",  mockpath);
    printf("result directory: '%s'\n", mockpath);

	char *config = "{  \"system\": { \
        \"base\": {},\
        \"disk\": {},\
        \"network\": {},\
        \"cadvisor\": {},\
        \"cpuavg\": {\
          \"period\": 5\
        },\
        \"services\": [    ],\
        \"process\": [ \"beam.smp\"  ],\
        \"auditd\": {},\
        \"firewall\": {},\
        \"packages\": []\
      }\
    }";

    http_api_v1(NULL, NULL, config);
    get_system_metrics();

    metric_test_run(CMP_EQUAL, "process_match", "process_match", 1);
    metric_test_run(CMP_GREATER, "cpu_usage_time", "cpu_usage_time", 0);
    metric_test_run(CMP_GREATER, "cores_num", "cores_num", 0);

    free(cwd);
    free(mockpath);
    free(bin_copy);
}
