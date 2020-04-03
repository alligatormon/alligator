#include <stdio.h>
void tls_debug(void *ctx, int level, const char *file, int line, const char *str)
{
	const char *p, *basename;

	for(p = basename = file; *p != '\0'; p++)
		if(*p == '/' || *p == '\\')
			basename = p + 1;

	fprintf((FILE *) ctx, "%s:%04d: |%d| %s", basename, line, level, str);
	fflush((FILE *) ctx);
}
