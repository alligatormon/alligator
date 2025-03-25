#include "main.h"
#ifdef __linux__
#include <stdio.h>
#include <dirent.h>
#include <inttypes.h>
#include <jansson.h>
#include <string.h>
#include <linux/sched.h>
#include "cadvisor/ns.h"
#include "cadvisor/metrics.h"
#include "cadvisor/libvirt.h"
#include "common/logs.h"
#include <sys/mount.h>
#include <sched.h>
int unshare(int flags);
extern aconf *ac;

typedef struct cnt_labels {
	char *id;
	char *name;
	char *namespace;
	char *image;
	char *cgroupsPath;
	char *container;
	char *pod;
} cnt_labels;

char *podman_get_container_json_from_bundle_path(char *bundle_path)
{
	//tests/mock/linux/home/test/.local/share/containers/storage/vfs-containers/866c2909f9882564daab2fa9164666e273ee78443d2696abc93e161701883e62/userdata
	//tests/mock/linux/home/test/.local/share/containers/storage/vfs-containers/containers.json
	char *cntsdir = strstr(bundle_path, "/vfs-containers/");
	uint64_t retsize = 16;
	if (!cntsdir)
	{
		retsize = 20;
		cntsdir = strstr(bundle_path, "/overlay-containers/");
		if (!cntsdir)
		{
			cntsdir = strstr(bundle_path, "/io.containerd.runtime");
			if (!cntsdir)
				return NULL;

			char* ret_path = malloc(255);
			size_t last_byte = strlcpy(ret_path, bundle_path, 255);
			strlcpy(ret_path+last_byte, "/config.json", 255 - last_byte);
			return ret_path;
		}
	}

	++cntsdir;
	cntsdir += strcspn(cntsdir, "/");
	size_t json_path_size = cntsdir - bundle_path;

	char* ret_path = malloc(json_path_size+retsize+1);
	strlcpy(ret_path, bundle_path, json_path_size+1);
	strlcpy(ret_path+json_path_size, "/containers.json", retsize);

	return ret_path;
}

char *runcv2_get_container_json_from_bundle_path(char *bundle_path)
{
	char *cntsdir = strstr(bundle_path, "/vfs-containers/");
	uint64_t retsize = 16;
	if (!cntsdir)
	{
		retsize = 20;
		cntsdir = strstr(bundle_path, "/overlay-containers/");
		if (!cntsdir)
			return NULL;
	}

	++cntsdir;
	cntsdir += strcspn(cntsdir, "/");
	size_t json_path_size = cntsdir - bundle_path;

	char* ret_path = malloc(json_path_size+retsize+1);
	strlcpy(ret_path, bundle_path, json_path_size+1);
	strlcpy(ret_path+json_path_size, "/containers.json", retsize);

	return ret_path;
}

cnt_labels* podman_get_labels(char *path, char *find_id, json_t *cgroup_main_path)
{
	//'.[].id,.[].names,.[].metadata'
	FILE *fd = fopen(path, "r");
	if (!fd)
		return NULL;

	size_t file_size = get_file_size(path);
	char *buf = malloc(file_size+1);
	if (!fread(buf, file_size, 1, fd))
	{
		free(buf);
		fclose(fd);
		return NULL;
	}
	buf[file_size] = 0;

	json_error_t error;
	json_t *root = json_loads(buf, 0, &error);
	if (!root)
	{
		fprintf(stderr, "podman_get_labels json error on line %d: %s\n", error.line, error.text);
		fclose(fd);
		return NULL;
	}

	uint64_t cnt_size = json_array_size(root);
	if (cnt_size)
	{
		for (uint64_t i = 0; i < cnt_size; i++)
		{
			json_t *obj = json_array_get(root, i);

			json_t *json_id = json_object_get(obj, "id");
			if (!json_id)
				continue;

			char *id = (char*)json_string_value(json_id);
			if (strcmp(id, find_id))
				continue;


			json_t *json_metadata = json_object_get(obj, "metadata");
			if (!json_metadata)
				continue;

			char *metadata = (char*)json_string_value(json_metadata);
			json_t *metadata_root = json_loads(metadata, 0, &error);
			if (!metadata_root)
			{
				fprintf(stderr, "podman_get_labels 2 json error on line %d: %s\n", error.line, error.text);
				continue;
			}

			json_t *json_name = json_object_get(metadata_root, "name");
			if (!json_name)
				continue;

			char *name = (char*)json_string_value(json_name);

			json_t *json_image_name = json_object_get(metadata_root, "image-name");
			if (!json_image_name)
				continue;

			char *image_name = (char*)json_string_value(json_image_name);

			cnt_labels* lbls = calloc(1, sizeof(*lbls));
			lbls->id = strdup(id);
			lbls->name = strdup(name);
			lbls->image = strdup(image_name);

			fclose(fd);
			free(buf);
			json_decref(metadata_root);
			json_decref(root);
			return lbls;
		}
	}
	else {
		json_t *annotations = json_object_get(root, "annotations");
		json_t *linux_j = json_object_get(root, "linux");
		json_t *type_j = json_object_get(annotations, "io.kubernetes.cri.container-type");
		const char *type = json_string_value(type_j);
		if (type && !strcmp(type, "sandbox")) {
			fclose(fd);
			json_decref(root);
			return NULL;
		}

		json_t *name_j = json_object_get(annotations, "io.kubernetes.cri.container-name");
		const char *name = json_string_value(name_j);

		json_t *image_j = json_object_get(annotations, "io.kubernetes.cri.image-name");
		const char *image = json_string_value(image_j);

		json_t *namespace_j = json_object_get(annotations, "io.kubernetes.cri.sandbox-namespace");
		const char *namespace = json_string_value(namespace_j);

		json_t *container_j = json_object_get(annotations, "io.kubernetes.cri.container-name");
		const char *container = json_string_value(container_j);

		json_t *pod_j = json_object_get(annotations, "io.kubernetes.cri.sandbox-name");
		const char *pod = json_string_value(pod_j);

		const char *cgroupsPath = NULL;
		json_t *cgroupsPath_j = json_object_get(linux_j, "cgroupsPath");
		if (cgroup_main_path) {
			cgroupsPath = json_string_value(cgroup_main_path);
			cgroupsPath = strstr(cgroupsPath, "fs/cgroup/");
			cgroupsPath += 10;
		}
		else
			cgroupsPath = json_string_value(cgroupsPath_j);

		cnt_labels* lbls = calloc(1, sizeof(*lbls));
		if (name)
			lbls->name = strdup(name);
		else
			lbls->name = strdup("");
		if (image)
			lbls->image = strdup(image);
		else
			lbls->image = strdup("");

		if (namespace)
			lbls->namespace = strdup(namespace);
		if (container)
			lbls->container = strdup(container);
		if (pod)
			lbls->pod = strdup(pod);
		if (cgroupsPath)
			lbls->cgroupsPath = strdup(cgroupsPath);

		fclose(fd);
		free(buf);
		json_decref(root);
		return lbls;
	}
	fclose(fd);

	free(buf);
	json_decref(root);
	return NULL;
}

void podman_parse(FILE *fd, size_t fd_size, char *fname)
{
	char *buf = malloc(fd_size+1);
	if (!fread(buf, fd_size, 1, fd))
	{
		free(buf);
		return;
	}
	buf[fd_size] = 0;
	//printf("fd_size %zu:\n'%s'\n", fd_size, buf);

	json_error_t error;
	json_t *root = json_loads(buf, JSON_DECODE_INT_AS_REAL, &error);
	if (!root)
	{
		fprintf(stderr, "podman_parse file %s: json error on line %d: %s\n", fname, error.line, error.text);
		free(buf);
		return;
	}

	json_t *cgroup_paths = json_object_get(root, "cgroup_paths");
	json_t *cgroup_main_path = json_object_get(cgroup_paths, "");
	json_t *json_id = json_object_get(root, "id");
	char *cnt_id = (char*)json_string_value(json_id);

	json_t *config = json_object_get(root, "config");
	json_t *labels = json_object_get(config, "labels");

	char label_name[255];
	char *label_value;
	//printf("ID: %s\n", cnt_id);
	uint64_t labels_size = json_array_size(labels);
	if (labels_size)
	{
		for (uint64_t i = 0; i < labels_size; i++)
		{
			json_t *labels_obj = json_array_get(labels, i);
			char *labels_str = (char*)json_string_value(labels_obj);

			label_value = strstr(labels_str, "=");
			if (!label_value)
				continue;

			strlcpy(label_name, labels_str, label_value-labels_str+1);
			label_value += strspn(label_value, "= ");

			//printf("\tlabel '%s' = '%s'\n", label_name, label_value);
			if (!strcmp(label_name, "bundle"))
			{
				char *container_json = podman_get_container_json_from_bundle_path(label_value);
                //printf("container_json: %s\n", container_json);
				if (container_json)
				{
					//printf("container json: '%s'\n", container_json);
					cnt_labels* podman_labels = podman_get_labels(container_json, cnt_id, cgroup_main_path);
					if (podman_labels)
					{
						char cgroup_cnt_id[255];
						if (podman_labels->id)
							snprintf(cgroup_cnt_id, 255, "libpod-%s.scope", podman_labels->id);
						else
							snprintf(cgroup_cnt_id, 255, "%s", cnt_id);

						if (podman_labels->cgroupsPath) {
							cadvisor_scrape(NULL, podman_labels->cgroupsPath, "", cgroup_cnt_id, podman_labels->name, podman_labels->image, podman_labels->namespace, podman_labels->pod, podman_labels->container, NULL);
						} else {
							cadvisor_scrape(NULL, NULL, "machine.slice", cgroup_cnt_id, podman_labels->name, podman_labels->image, podman_labels->namespace, NULL, NULL, NULL);
						}

						free(podman_labels->name);
						free(podman_labels->image);
						if (podman_labels->id)
							free(podman_labels->id);
						if (podman_labels->namespace)
							free(podman_labels->namespace);
						if (podman_labels->cgroupsPath)
							free(podman_labels->cgroupsPath);
						if (podman_labels->container)
							free(podman_labels->container);
						if (podman_labels->pod)
							free(podman_labels->pod);
						free(podman_labels);
					}
					free(container_json);
				}
			}
		}
	}
	json_decref(root);
	free(buf);
}

void runc_labels(char *rundir, char *path_template)
{
	DIR *rd;
	DIR *ud;
	FILE *fd;

	struct dirent *rd_entry;
	struct dirent *ud_entry;

	char runcdir[255];
	char userdir[255];
	char statefile[255];
	snprintf(runcdir, 255, path_template, rundir);
	//printf("0 opendir: %s\n", runcdir);
	rd = opendir(runcdir);
	if (rd)
	{
		while((rd_entry = readdir(rd)))
		{
			if (rd_entry->d_name[0] == '.')
				continue;

			snprintf(statefile, 255, "%s/%s/state.json", runcdir, rd_entry->d_name);
			size_t statefile_size = get_file_size(statefile);
			fd = fopen(statefile, "r");
			if (fd)
			{
				podman_parse(fd, statefile_size, statefile);

				fclose(fd);
			}
		}
		closedir(rd);
	}

	snprintf(userdir, 255, "%s/user", rundir);
	//printf("1 opendir: %s\n", userdir);
	ud = opendir(userdir);
	if (ud)
	{
		while((ud_entry = readdir(ud)))
		{
			if (ud_entry->d_name[0] == '.')
				continue;

			//puts(entry->d_name);
			snprintf(runcdir, 255, "%s/%s/runc", userdir, ud_entry->d_name);
			//printf("opendir: %s\n", runcdir);
			rd = opendir(runcdir);
			if (rd)
			{
				while((rd_entry = readdir(rd)))
				{
					if (rd_entry->d_name[0] == '.')
						continue;

					snprintf(statefile, 255, "%s/%s/state.json", runcdir, rd_entry->d_name);
					size_t statefile_size = get_file_size(statefile);
					fd = fopen(statefile, "r");
					if (fd)
					{
						puts(statefile);
						podman_parse(fd, statefile_size, statefile);

						fclose(fd);
					}
				}
				closedir(rd);
			}
		}
		closedir(ud);

	}
}

void openvz7_labels()
{
	char beancounters[255];
	snprintf(beancounters, 255, "%s/user_beancounters", ac->system_procfs);
	struct stat path_stat;
	if (stat(beancounters, &path_stat))
		return;

	char nsdir[1000];
	char nsfile[1000];
	DIR *rd;
	DIR *ethd;
	struct dirent *rd_entry;
	struct dirent *ethd_entry;

	snprintf(nsdir, 1000, "%s/netns", ac->system_rundir);

	rd = opendir(nsdir);
	if (rd)
	{
		while((rd_entry = readdir(rd)))
		{
			if (rd_entry->d_name[0] == '.')
				continue;

			snprintf(nsfile, 1000, "%s/%s", nsdir, rd_entry->d_name);
			int fd = open(nsfile, O_RDONLY);
			if (fd < 0)
				continue;

			int rc = net_ns_mount(fd, rd_entry->d_name);
			if (!rc)
				continue;

			close(fd);

			char cad_id[255];
			snprintf(cad_id, 255, "/%s", rd_entry->d_name);

			ethd = opendir("/var/lib/alligator/nsmount/class/net/");
			if (ethd)
			{
				while((ethd_entry = readdir(ethd)))
				{
					if (ethd_entry->d_name[0] == '.')
						continue;

					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_dropped", "container_network_receive_packets_dropped_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_bytes", "container_network_receive_bytes_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "tx_bytes", "container_network_transmit_bytes_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "tx_packets", "container_network_transmit_packets_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_packets", "container_network_receive_packets_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "tx_errors", "container_network_transmit_errors_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_errors", "container_network_receive_errors_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "tx_dropped", "container_network_transmit_packets_dropped_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
					cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_dropped", "container_network_receive_packets_dropped_total", rd_entry->d_name, rd_entry->d_name, NULL, cad_id, NULL, NULL, NULL, NULL);
				}
				closedir(ethd);
			}
			umount("/var/lib/alligator/nsmount");
			unshare(CLONE_NEWNET);

			cadvisor_scrape(NULL, NULL, "", rd_entry->d_name, rd_entry->d_name, NULL, NULL, NULL, NULL, NULL);
		}
		closedir(rd);
	}
}

void lxc_labels()
{
	char lxcdir[1000];
	char lxccgroup[1000];
	DIR *rd;
	struct dirent *rd_entry;

	snprintf(lxcdir, 1000, "%s/fs/cgroup/pids/lxc/", ac->system_sysfs);

	rd = opendir(lxcdir);
	if (rd)
	{
		while((rd_entry = readdir(rd)))
		{
			if (rd_entry->d_name[0] == '.')
				continue;

			snprintf(lxccgroup, 999, "%s/%s", lxcdir, rd_entry->d_name);

			struct stat path_stat;
			stat(lxccgroup, &path_stat);

			if (!S_ISDIR(path_stat.st_mode))
				continue;

			cadvisor_scrape(NULL, NULL, "lxc", rd_entry->d_name, rd_entry->d_name, NULL, NULL, NULL, NULL, NULL);
		}
		closedir(rd);
	}
}

void nspawn_labels()
{
	char nspawndir[1000];
	char nspawncgroup[1000];
	DIR *rd;
	struct dirent *rd_entry;

	snprintf(nspawndir, 1000, "%s/fs/cgroup/cpu,cpuacct/machine.slice/", ac->system_sysfs);

	rd = opendir(nspawndir);
	if (rd)
	{
		puts("NSPAWN scraper!");
		while((rd_entry = readdir(rd)))
		{
			if (rd_entry->d_name[0] == '.')
				continue;

			snprintf(nspawncgroup, 1000, "%s/%s", nspawndir, rd_entry->d_name);

			struct stat path_stat;
			stat(nspawncgroup, &path_stat);

			if (!S_ISDIR(path_stat.st_mode))
				continue;

			cadvisor_scrape(NULL, NULL, "machine.slice", rd_entry->d_name, rd_entry->d_name, NULL, NULL, NULL, NULL, NULL);
		}
		closedir(rd);
	}
}

void docker_labels(char *metrics, size_t size, context_arg *carg)
{
	r_time ts_start = setrtime();
	carglog(ac->cadvisor_carg, L_TRACE, "DOCKER scraper!\n");
	carglog(ac->cadvisor_carg, L_TRACE, "%s\n", metrics);
	carglog(ac->cadvisor_carg, L_TRACE, "END\n");

	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "docker_labels json error on line %d: %s\n", error.line, error.text);
		return;
	}

	uint64_t cnt_size = json_array_size(root);
	if (cnt_size)
	{
		for (uint64_t i = 0; i < cnt_size; i++)
		{
			r_time docker_start = setrtime();
			json_t *obj = json_array_get(root, i);

			json_t *json_id = json_object_get(obj, "Id");
			if (!json_id)
				continue;

			char *id = (char*)json_string_value(json_id);

			json_t *json_image = json_object_get(obj, "Image");
			if (!json_image)
				continue;

			char *image = (char*)json_string_value(json_image);

			json_t *json_names = json_object_get(obj, "Names");
			if (!json_names)
				continue;

			json_t *names = json_array_get(json_names, 0);
			char *name = (char*)json_string_value(names);
			char *name_str = name + 1;

			json_t *json_labels = json_object_get(obj, "Labels");
			if (!json_labels)
				continue;

			const char *label_key;
			json_t *label_value;

			int is_k8s = 0;
			char kubepath[255];
			char kubenamespace[255];
			char kubepod[255];
			char kubecontainer[255];

			json_object_foreach(json_labels, label_key, label_value)
			{
				if (json_typeof(label_value) == JSON_STRING)
				{
					char *str_value = (char*)json_string_value(label_value);
					carglog(ac->cadvisor_carg, L_TRACE, "\tlabels: '%s': '%s'\n", label_key, str_value);
					
					if (!strcmp(label_key, "io.kubernetes.pod.uid"))
					{
						uint8_t success = 0;

						size_t str_value_len = json_string_length(label_value);

						char underscore_value[255];
						for (uint64_t i = 0; i < str_value_len; i++)
							if (str_value[i] == '-')
								underscore_value[i] = '_';
							else
								underscore_value[i] = str_value[i];

						is_k8s = 1;

						snprintf(kubepath, 254, "kubepods.slice/kubepods-burstable.slice/kubepods-burstable-pod%s.slice/docker-%s.scope", underscore_value, id);
						char fullpath[255];
						snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
						struct stat path_stat;
						if (stat(fullpath, &path_stat))
						{
							snprintf(kubepath, 254, "kubepods.slice/kubepods-besteffort.slice/kubepods-besteffort-pod%s.slice/docker-%s.scope", underscore_value, id);
							snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
							if (stat(fullpath, &path_stat))
							{
								snprintf(kubepath, 254, "kubepods.slice/kubepods-pod%s.slice/docker-%s.scope/", underscore_value, id);
								snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
								if (!stat(fullpath, &path_stat))
									success = 1;
							}

						}

						if (!success)
						{
							snprintf(kubepath, 254, "kubepods/kubepods-burstable/kubepods-burstable-pod%s/%s", str_value, id);
							snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
							if (stat(fullpath, &path_stat))
							{
								snprintf(kubepath, 254, "kubepods.slice/kubepods-besteffort.slice/kubepods-besteffort-pod%s.slice/docker-%s.scope", str_value, id);
								snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
								if (stat(fullpath, &path_stat))
								{
									snprintf(kubepath, 254, "kubepods.slice/kubepods-pod%s.slice/docker-%s.scope/", str_value, id);
									snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
									if (!stat(fullpath, &path_stat))
										success = 1;
								}
							}
						}

						if (!success)
						{
							snprintf(kubepath, 254, "kubepods/pod%s/%s", str_value, id);
							snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
							if (stat(fullpath, &path_stat))
							{
								snprintf(kubepath, 254, "kubepods/burstable/pod%s/%s", str_value, id);
								snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
								if (stat(fullpath, &path_stat))
								{
									snprintf(kubepath, 254, "kubepods/besteffort/pod%s/%s", str_value, id);
									snprintf(fullpath, 254, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, kubepath);
									if (!stat(fullpath, &path_stat))
										success = 1;
								}
							}
						}

					}
					else if (!strcmp(label_key, "io.kubernetes.pod.namespace"))
						strlcpy(kubenamespace, str_value, 255);
					else if (!strcmp(label_key, "io.kubernetes.pod.name"))
						strlcpy(kubepod, str_value, 255);
					else if (!strcmp(label_key, "io.kubernetes.container.name"))
						strlcpy(kubecontainer, str_value, 255);
				}
			}

			if (!is_k8s)
			{
				carglog(ac->cadvisor_carg, L_TRACE, "\tname: %s, image: %s, path: %s\n", name_str, image, id);
				cadvisor_scrape(NULL, NULL, "docker", id, name_str, image, NULL, NULL, NULL, NULL);
				char cgroup_path[255];
				snprintf(cgroup_path, 254, "system.slice/docker-%s.scope", id);
				cadvisor_scrape(NULL, cgroup_path, "", id, name_str, image, NULL, NULL, NULL, NULL);
			}
			else
			{
				carglog(ac->cadvisor_carg, L_TRACE, "\tname: %s, image: %s, path: %s\n", name_str, image, kubepath);
				cadvisor_scrape(NULL, NULL, "", kubepath, name_str, image, kubenamespace, kubepod, kubecontainer, NULL);
			}
			r_time docker_end = setrtime();
			double scrape_total_time = getrtime_sec_float(ts_start, docker_end);
			double docker_time = getrtime_sec_float(docker_start, docker_end);
			carglog(ac->system_carg, L_TRACE, "cadvisor scrape metrics: docker: image:'%lf' total:'%lf' \tname: %s, image: %s, path: %s\n", docker_time, scrape_total_time, name_str, image, kubepath);
		}
	}
	json_decref(root);
	carg->parser_status = 1;
	r_time ts_end = setrtime();
	double scrape_time = getrtime_sec_float(ts_start, ts_end);
	carglog(ac->system_carg, L_DEBUG, "cadvisor scrape metrics: docker: '%lf'\n", scrape_time);
}

void cgroup_v2_machines()
{
	char v2dir[1001];
	char v2cgroup[1001];
	DIR *rd;
	struct dirent *rd_entry;

	snprintf(v2dir, 1000, "%s/fs/cgroup/machine.slice/", ac->system_sysfs);

	rd = opendir(v2dir);
	if (rd)
	{
		while((rd_entry = readdir(rd)))
		{
			if (rd_entry->d_name[0] == '.')
				continue;

			snprintf(v2cgroup, 1000, "%s/%s", v2dir, rd_entry->d_name);

			struct stat path_stat;
			stat(v2cgroup, &path_stat);

			if (!S_ISDIR(path_stat.st_mode))
				continue;


			char type[255] = { 0 };
			char name[255] = { 0 };
			char *contname = strdup(rd_entry->d_name);
			int num = 0;
			char *ptrname = NULL;
			char *endname = NULL;
			int j = 0;
			int j_len = 0;
			char libvirt_index[19] = {0};
			int is_libvirt = 0;
			char check_libvirt_path[1001];
			snprintf(check_libvirt_path, 1000, "%s/libvirt", v2cgroup);
			DIR *check_libvirt = opendir(check_libvirt_path);
			char *pass_libvirt_id = NULL;
			if (check_libvirt)
			{
				pass_libvirt_id = libvirt_index;
				is_libvirt = 1;
				closedir(check_libvirt);
			}

			for (int i = 0; contname[i]; ++i) {
				if (!strncmp(contname+i, "\\x2d", 4)) {
					++num;
					contname[i] = '-';
					strlcpy(contname+i+1, contname+i+4, 255);

					if (num == 1) {
						strlcpy(type, contname, i+1);
						j = i + 1;
						if (!is_libvirt)
							ptrname = contname+i+1;
					}
					if (num == 2) {
						j_len = i - j;
						if (is_libvirt)
							ptrname = contname+i+1;
					}
					if (num == 3) {
						strlcpy(libvirt_index, contname + j, j_len + 1);
					}
				}

				if (!strncmp(contname+i, ".scope", 6)) {
					endname = contname+i;
				}
			}

			if (ptrname && endname)
				strlcpy(name, ptrname, endname - ptrname + 1);
			else
				strlcpy(name, contname, 255);
			carglog(ac->cadvisor_carg, L_INFO, "cgroup v2 scrape name: '%s', d_name: '%s', contname: '%s', type: '%s', libvirt index: %s/%s, check_libvirt: '%s'\n", name, rd_entry->d_name, contname, type, libvirt_index, pass_libvirt_id, check_libvirt_path);
			cadvisor_scrape(NULL, NULL, "machine.slice", rd_entry->d_name, name, NULL, NULL, NULL, NULL, pass_libvirt_id);
			free(contname);
		}
		closedir(rd);
	}
}

void cadvisor_metrics()
{
	runc_labels(ac->system_rundir, "%s/runc");
	runc_labels(ac->system_rundir, "%s/containerd/runc/k8s.io/");
	//runc_labels(ac->system_rundir, "%s/docker/runtime-runc/moby/");
	openvz7_labels();
	lxc_labels();
	nspawn_labels();
	cgroup_v2_machines();
	libvirt();
}
#endif

#ifdef __FreeBSD__
void docker_labels(char *metrics, size_t size, context_arg *carg) {};
#endif
