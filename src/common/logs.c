#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <common/selector.h>
#include <common/logs.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "main.h"

extern aconf *ac;

uint64_t get_log_level_by_name(const char *val, size_t len) {
	uint64_t level = L_OFF;
	if (sisdigit(val)) {
		level = strtoull(val, NULL, 10);
	} else {
		if (!strncasecmp(val, "off", 3)) {
			level = L_OFF;
		} else if (!strncasecmp(val, "fatal", 5)) {
			level = L_FATAL;
		} else if (!strncasecmp(val, "error", 5)) {
			level = L_ERROR;
		} else if (!strncasecmp(val, "warn", 4)) {
			level = L_WARN;
		} else if (!strncasecmp(val, "info", 4)) {
			level = L_INFO;
		} else if (!strncasecmp(val, "debug", 5)) {
			level = L_DEBUG;
		} else if (!strncasecmp(val, "trace", 5)) {
			level = L_TRACE;
		}
	}

	return level;
}

char* get_log_level_by_id(uint64_t id) {
	char *ret;
	if (id == L_OFF) {
		ret = "off";
	} else if (id == L_FATAL) {
		ret = "fatal";
	} else if (id == L_ERROR) {
		ret = "error";
	} else if (id == L_WARN) {
		ret = "warn";
	} else if (id == L_INFO) {
		ret = "info";
	} else if (id == L_DEBUG) {
		ret = "debug";
	} else if (id == L_TRACE) {
		ret = "trace";
	} else {
		ret = NULL;
	}

	return ret;
}

void log_init() {
	if (!ac->log_dest)
		ac->log_socket = fileno(stdout);
	else if (!strncmp(ac->log_dest, "stdout", 6))
		ac->log_socket = fileno(stdout);
	else if (!strncmp(ac->log_dest, "stderr", 6))
		ac->log_socket = fileno(stderr);
	else if (!strncmp(ac->log_dest, "udp://", 6)) {
		host_aggregator_info *hi = parse_url(ac->log_dest, strlen(ac->log_dest));

		if (ac->log_host)
			free(ac->log_host);

		ac->log_host = strdup(hi->host);
		//string *data = aggregator_get_addr(NULL, ac->log_host, DNS_TYPE_A, DNS_CLASS_IN);
		bzero(&ac->logsoaddr, sizeof(ac->logsoaddr)); 
    		ac->logsoaddr.sin_addr.s_addr = inet_addr(ac->log_host); 
    		ac->logsoaddr.sin_port = htons(ac->log_port); 
    		ac->logsoaddr.sin_family = AF_INET; 
		ac->log_socket = socket(AF_INET, SOCK_DGRAM, 0);

		if(connect(ac->log_socket, (struct sockaddr *)&ac->logsoaddr, sizeof(ac->logsoaddr)) < 0)
		{
			printf("log connect failed\n");
			ac->log_socket = fileno(stdout);
		}

		url_free(hi);
	}
	else if (!strncmp(ac->log_dest, "file://", 7)) {
		host_aggregator_info *hi = parse_url(ac->log_dest, strlen(ac->log_dest));

		if (ac->log_host)
			free(ac->log_host);

		ac->log_socket = open(hi->host, O_WRONLY|O_APPEND|O_CREAT|O_NONBLOCK, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

		url_free(hi);
	} else {
		ac->log_socket = fileno(stdout);
	}
}

void wrlog(int level, int priority, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    printf("check %d>=%d \n", level, priority);
    if(level >= priority)
    {
            vdprintf(ac->log_socket, format, args);
	    puts("OK");
    }

    va_end(args);
}

//int main() {
//	write_log(1, "hello %s\n", "pidor");
//}
