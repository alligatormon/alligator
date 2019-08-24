#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "url.h"
#include "base64.h"
host_aggregator_info *parse_url (char *str, size_t len)
{
	host_aggregator_info *hi = malloc(sizeof(*hi));
	hi->auth = 0;
	char *tmp;
	if ( !strncmp(str, "http://", 7) )
	{
		int64_t k, l, m;
		tmp = str+7;
		hi->proto = APROTO_HTTP;

		k = strcspn(tmp, ":");
		l = strcspn(tmp, "@");
		m = strcspn(tmp, "/");
		if ( k < l && l < m )
		{
			hi->proto = APROTO_HTTP_AUTH;
			//unsigned char *buf = strndup(tmp, l);
			size_t sz;
			hi->auth = base64_encode(tmp, l, &sz);
			//free(buf);
			tmp += l +1;
			k = strcspn(tmp, ":");
			m = strcspn(tmp, "/");
			l = strcspn(tmp, "?");
			if ( k > m )
				k = m;
			if ( k > l )
				k = l;
		}
		else if (k == l)
			k = m;

		if ( tmp[k-1] == '/' )
			hi->host = strndup(tmp, k-1);
		else
			hi->host = strndup(tmp, k);

		tmp += k;
		k = strcspn(tmp, "/");
		l = strcspn(tmp, "?");
		if ( k > l )
			k = l;

		if (((str + len) - tmp) > 2)
		{
			if (strstr(tmp, ":"))
			{
				strlcpy(hi->port, tmp+1, k);
				tmp += k;
			}
			else
				strlcpy(hi->port, "80", 3);

			hi->query = strdup(tmp);
		}
		else
		{
			strlcpy(hi->port, "80", 3);
			hi->query = strdup("/");
		}
		//printf("host %s\n", hi->host);
		//printf("query %s\n", hi->query);
		//printf("port %s\n", hi->port);

	}
	if ( !strncmp(str, "https://", 8) )
	{
		int64_t k, l, m;
		tmp = str+8;
		hi->proto = APROTO_HTTPS;

		k = strcspn(tmp, ":");
		l = strcspn(tmp, "@");
		m = strcspn(tmp, "/");
		if ( k < l && l < m )
		{
			hi->proto = APROTO_HTTPS_AUTH;
			//unsigned char *buf = strndup(tmp, l);
			size_t sz;
			hi->auth = base64_encode(tmp, l, &sz);
			//free(buf);
			tmp += l +1;
			k = strcspn(tmp, ":");
			m = strcspn(tmp, "/");
			l = strcspn(tmp, "?");
			if ( k > m )
				k = m;
			if ( k > l )
				k = l;
		}
		else if (k == l)
			k = m;

		if ( tmp[k-1] == '/' )
			hi->host = strndup(tmp, k-1);
		else
			hi->host = strndup(tmp, k);

		tmp += k;
		k = strcspn(tmp, "/");
		l = strcspn(tmp, "?");
		if ( k > l )
			k = l;

		if (((str + len) - tmp) > 2)
		{
			if (strstr(tmp, ":"))
			{
				strlcpy(hi->port, tmp+1, k);
				tmp += k;
			}
			else
				strlcpy(hi->port, "443", 4);

			hi->query = strdup(tmp);
		}
		else
		{
			strlcpy(hi->port, "443", 4);
			hi->query = strdup("/");
		}
		//printf("proto %d\n", hi->proto);
		//printf("host %s\n", hi->host);
		//printf("query %s\n", hi->query);
		//printf("port %s\n", hi->port);

	}
	if ( !strncmp(str, "fastcgi://", 10) )
	{
		int64_t k, l, m;
		tmp = str+10;
		hi->proto = APROTO_FCGI;

		k = strcspn(tmp, ":");
		l = strcspn(tmp, "@");
		m = strcspn(tmp, "/");
		if ( k < l && l < m )
		{
			hi->proto = APROTO_FCGI_AUTH;
			//unsigned char *buf = strndup(tmp, l);
			size_t sz;
			hi->auth = base64_encode(tmp, l, &sz);
			//free(buf);
			tmp += l +1;
			k = strcspn(tmp, ":");
			m = strcspn(tmp, "/");
			l = strcspn(tmp, "?");
			if ( k > m )
				k = m;
			if ( k > l )
				k = l;
		}

		if ( tmp[k-1] == '/' )
			hi->host = strndup(tmp, k-1);
		else
			hi->host = strndup(tmp, k);

		tmp += k + 1;
		k = strcspn(tmp, "/");
		l = strcspn(tmp, "?");
		if ( k > l )
			k = l;

		if ((tmp - (str + len)) > 2)
			strlcpy(hi->port, tmp, k+1);
		else
			strlcpy(hi->port, "80", 3);

		tmp += k;
		hi->query = strdup(tmp);
	}
	else if ( !strncmp(str, "tcp://", 6) )
	{
		int64_t k;
		int64_t t;
		tmp = str+6;
		hi->proto = APROTO_TCP;

		t = strcspn(tmp, "@");
		k = strcspn(tmp, ":");
		if ((t > k) && (t != len-6))
		{
			hi->user = strndup(tmp, k);
			hi->pass = strndup(tmp+k+1, t-k-1);
			tmp += t;
			++tmp;
			k = strcspn(tmp, ":");
		}
		else
		{
			hi->user = 0;
			hi->pass = 0;
		}

		hi->host = strndup(tmp, k);

		tmp += k;
		if (*tmp == ':')
			tmp++;
		else
		{
			free(hi->host);
			free(hi);
			return NULL;
		}

		k = strcspn(tmp, "\0");
		strlcpy(hi->port, tmp, k+1);
	}
	else if ( !strncmp(str, "tls://", 6) )
	{
		int64_t k;
		int64_t t;
		tmp = str+6;
		hi->proto = APROTO_TLS;

		t = strcspn(tmp, "@");
		k = strcspn(tmp, ":");
		if ((t > k) && (t != len-6))
		{
			hi->user = strndup(tmp, k);
			hi->pass = strndup(tmp+k+1, t-k-1);
			tmp += t;
			++tmp;
			k = strcspn(tmp, ":");
		}
		else
		{
			hi->user = 0;
			hi->pass = 0;
		}

		hi->host = strndup(tmp, k);

		tmp += k;
		if (*tmp == ':')
			tmp++;
		else
		{
			free(hi->host);
			free(hi);
			return NULL;
		}

		k = strcspn(tmp, "\0");
		strlcpy(hi->port, tmp, k+1);
	}
	else if ( !strncmp(str, "udp://", 6) )
	{
		int64_t k;
		tmp = str+6;
		hi->proto = APROTO_UDP;

		k = strcspn(tmp, ":");
		hi->host = strndup(tmp, k);

		tmp += k;
		if (*tmp == ':')
			tmp++;
		else
		{
			free(hi->host);
			free(hi);
			return NULL;
		}

		k = strcspn(tmp, "\0");
		strlcpy(hi->port, tmp, k+1);
	}
	else if ( !strncmp(str, "file://", 7) )
	{
		tmp = str+7;

		if (len <= 7)
		{
			free(hi);
			return NULL;
		}

		hi->proto = APROTO_FILE;
		hi->host = strdup(tmp);
		*(hi->port) = '0';
		*(hi->port+1) = 0;
	}
	else if ( !strncmp(str, "exec://", 7) )
	{
		tmp = str+7;

		if (len <= 7)
		{
			free(hi);
			return NULL;
		}

		hi->proto = APROTO_PROCESS;
		hi->host = strdup(tmp);
		*(hi->port) = '0';
		*(hi->port+1) = 0;
	}
	else if ( !strncmp(str, "unix://", 7) )
	{
		tmp = str+7;

		if (len <= 7)
		{
			free(hi);
			return NULL;
		}

		int64_t k, t;
		t = strcspn(tmp, "@");
		k = strcspn(tmp, ":");
		if ((t > k) && (t != len-6))
		{
			hi->user = strndup(tmp, k);
			hi->pass = strndup(tmp+k+1, t-k-1);
			tmp += t;
			++tmp;
			k = strcspn(tmp, ":");
		}

		hi->proto = APROTO_UNIX;
		hi->host = strdup(tmp);
	}
	else if ( !strncmp(str, "unixfcgi://", 11) )
	{
		tmp = str+11;

		if (len <= 11)
		{
			free(hi);
			return NULL;
		}

		int64_t k = strcspn(tmp, ":");

		hi->proto = APROTO_UNIXFCGI;
		hi->host = strndup(tmp, k);

		hi->query = strdup(tmp+k+1);
	}
	else if ( !strncmp(str, "unixgram://", 11) )
	{
		tmp = str+11;

		if (len <= 11)
		{
			free(hi);
			return NULL;
		}

		hi->proto = APROTO_UNIXGRAM;
		hi->host = strdup(tmp);
	}

	return hi;
}
