#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "dstructures/metric.h"
int validate_domainname(char *domainname, size_t len)
{
	if ( len < 2 )
		return 0;
	if ( len > 255 )
		return 0;

	int64_t point = 0, i;
	for ( i=0; i<len; i++ )
	{
		if ( domainname[i] == '.' )
		{
			if (i-point > 63)
				return 0;
			point = i;
		}
		else if ( isdigit(domainname[i]) )
			{}
		else if ( domainname[i] == '-' )
			{}
		else if ( !isalpha(domainname[i]) )
			return 0;
	}
	if ( point == 0 )
		return 0;
	if ( len-2 <= point )
		return 0;
	return 1;
}

int validate_path(char *path, size_t len)
{
	if ( len > 1000 )
		return 0;
#ifndef _WIN64
	if ( path[0] != '/' )
		return 0;
#endif
#ifdef __MACH__
	if ( len != strcspn(path, ":") )
		return 0;
#endif
#ifdef _WIN64
	if ( path[1] != ':' || path[1] != '\\' )
		return 0;
	if ( len != strcspn(path, "/:*\"<>|") )
		return 0;
#endif
	return 1;
}

int metric_name_validator(char *str, size_t sz)
{
	int64_t i;
	for (i=0; i<sz; i++)
		if ( isalpha(str[i]) || str[i] == '_' )
			continue;
		else if (isdigit(str[i]))
			continue;
		else
			return 0;

	return 1;
}

int metric_value_validator(char *str, size_t sz)
{
	int64_t i;
	int type = 0;
	for (i=0; i<sz; i++)
	{
		if ( isdigit(str[i]) || str[i] == '.' )
		{
			if ( isdigit(str[i]) && type != ALLIGATOR_DATATYPE_INT && type != ALLIGATOR_DATATYPE_DOUBLE)
				type = ALLIGATOR_DATATYPE_INT;
			else if (str[i] == '.' && type != ALLIGATOR_DATATYPE_DOUBLE)
				type = ALLIGATOR_DATATYPE_DOUBLE;
			else if (str[i] == '.' && type == ALLIGATOR_DATATYPE_DOUBLE)
				return 0;
			else if (type == ALLIGATOR_DATATYPE_INT || type == ALLIGATOR_DATATYPE_DOUBLE) {}
		}
		else
			return 0;
	}

	return type;
}
