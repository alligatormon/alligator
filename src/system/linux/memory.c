#include <dirent.h>
#include <netpacket/packet.h>
#include "main.h"
extern aconf *ac;

int parse_mem_field(char* label, char *id, char *find)
{
	char *ptr = strstr(label, find);
	if (!ptr)
		return 0;
	size_t size = 0;
	ptr += strcspn(ptr, "#");
	ptr += strspn(ptr, "#");
	size = strspn(ptr, "0123456789");
	return strlcpy(id, ptr, size+1);
}

void parse_mem_label(char* label, char* cpuid, char* channelid, char* slotid, char *branchid)
{
	str_tolower(label, 255);

	parse_mem_field(label, cpuid, "cpu");
	parse_mem_field(label, branchid, "chan");
	parse_mem_field(label, branchid, "branch");

	if (!parse_mem_field(label, slotid, "dimm"))
		parse_mem_field(label, slotid, "slot");
}

void get_memory_errors(char *edac, char* controller)
{
	struct dirent *entry;
	DIR *dp;

	dp = opendir(edac);
	if (!dp)
		return;

	while((entry = readdir(dp)))
	{
		if (strncmp(entry->d_name, "dimm", 4))
			continue;

		struct dirent *rowentry;
		DIR *rowdp;

		char rowname[512];
		char chname[769];
		snprintf(rowname, 511, "%s%s/", edac, entry->d_name);
		rowdp = opendir(rowname);
		if (!rowdp)
			continue;

		char label[255];
		char label_file[255];
		snprintf(label_file, 254, "%sdimm_label", rowname);
	 	getkvfile_str(label_file, label, 254);
		printf("open file %s\n", label_file);

		char cpuid[3];
		char channelid[3];
		char slotid[3];
		char branchid[3];
		parse_mem_label(label, cpuid, channelid, slotid, branchid);

		while((rowentry = readdir(rowdp)))
		{
				snprintf(chname, 255, "%s/%s", rowname, rowentry->d_name);
			if (strstr(rowentry->d_name, "_ce_count"))
			{
				snprintf(chname, 255, "%s/%s", rowname, rowentry->d_name);
				int64_t errors = getkvfile(chname);
				metric_add_labels6("memory_errors", &errors, DATATYPE_INT, ac->system_carg, "type", "correctable", "cpu", cpuid, "channel", channelid, "controller", controller, "slot", slotid, "branch", branchid);
			}

			if (strstr(rowentry->d_name, "_ue_count"))
			{
				snprintf(chname, 255, "%s/%s", rowname, rowentry->d_name);
				int64_t errors = getkvfile(chname);
				metric_add_labels6("memory_errors", &errors, DATATYPE_INT, ac->system_carg, "type", "uncorrectable", "cpu", cpuid, "channel", channelid, "controller", controller, "slot", slotid, "branch", branchid);
			}

			if (strstr(rowentry->d_name, "size"))
			{
				snprintf(chname, 255, "%s/%s", rowname, rowentry->d_name);
				int64_t errors = getkvfile(chname);
				metric_add_labels5("memory_dimm_size", &errors, DATATYPE_INT, ac->system_carg, "cpu", cpuid, "channel", channelid, "controller", controller, "slot", slotid, "branch", branchid);
			}
		}
		closedir(rowdp);
	}
	closedir(dp);
}

void memory_errors_by_controller() {
	struct dirent *entry;
	DIR *dp;
	char edac[255];
	char edacdir[255];
	snprintf(edac, 255, "%s/devices/system/edac/mc/", ac->system_sysfs);

	dp = opendir(edac);
	if (!dp)
		return;

	while((entry = readdir(dp)))
	{
		if (strncmp(entry->d_name, "mc", 2))
			continue;
		snprintf(edacdir, 254, "%s/%s/", edac, entry->d_name);
		get_memory_errors(edacdir, entry->d_name);
	}
	closedir(dp);
}
