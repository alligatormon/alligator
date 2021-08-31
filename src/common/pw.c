//#define _POSIX_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
//#include <uuid/uuid.h>
#include <stdlib.h>

char* get_username_by_uid(uid_t uid) {
	struct passwd *p = getpwuid(uid);
	
	if (!p)
	{
		perror("getpwuid() error");
		return NULL;
	}
	else {
		//printf("getpwuid() returned the following info for uid %d:\n", (int) uid);
		//printf("  pw_name  : %s\n",       p->pw_name);
		//printf("  pw_uid   : %d\n", (int) p->pw_uid);
		//printf("  pw_gid   : %d\n", (int) p->pw_gid);
		//printf("  pw_dir   : %s\n",       p->pw_dir);
		//printf("  pw_shell : %s\n",       p->pw_shell);
		return strdup(p->pw_name);
	}
}

char* get_groupname_by_gid(gid_t gid) {
	struct group *g = getgrgid(gid);
	
	if (!g)
	{
		perror("getgrgid() error");
		return NULL;
	}
	else {
		return strdup(g->gr_name);
	}
}

uid_t get_uid_by_username(char *username)
{
	struct passwd *pwd = getpwnam(username);
	if (!pwd)
	{
		perror("getpwnam:");
		return -1;
	}
	else
	{
		uid_t retuid = pwd->pw_uid;
		return retuid;
	}
}

gid_t get_gid_by_groupname(char *groupname)
{
	struct group *g = getgrnam(groupname);
	if (!g)
	{
		perror("getgrnam:");
		return -1;
	}
	else
	{
		gid_t retgid = g->gr_gid;
		return retgid;
	}
}
