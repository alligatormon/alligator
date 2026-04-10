#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#define XMLNODE_MAXSIZE_NAME 1000

char *get_xml_name(char *str, size_t *size, size_t *end)
{
	char *nodeptr;
	int64_t i;
	for(i = 0; i<*size; i++)
	{
		i = strcspn(str+i, "<")+i;
		if ((size_t)i >= *size)
			break;
		if (str[i] == '<' && ((size_t)i + 1 < *size) && str[i+1] != '/')
		{
			++i;
			*size = strcspn(str+i, " \t>");
			*end = strcspn(str+i, ">");
			nodeptr = str+i;
			return nodeptr;
		}
		else if (str[i] != '<')
		{
			*size = 0;
			return NULL;
		}
	}
	*size = 0;
	return NULL;
}

int64_t get_xml_node(char *str, size_t size, char *name, size_t *name_size, char *value, size_t *value_size, size_t *end)
{
	size_t name_cap = *name_size;
	size_t value_cap = *value_size;
	*name_size = size;
	size_t name_end;
	char *nameptr;
	char *tmp = nameptr = get_xml_name(str, name_size, &name_end);

	if (!tmp)
	{
		*name_size = 0;
		*value_size = 0;
		*end = 0;
		return 0;
	}
	if (!name_cap || *name_size >= name_cap)
	{
		*name_size = 0;
		*value_size = 0;
		*end = 0;
		return 0;
	}
	if (*name_size + 4 >= XMLNODE_MAXSIZE_NAME)
	{
		*name_size = 0;
		*value_size = 0;
		*end = 0;
		return 0;
	}
	strlcpy(name, tmp, *name_size+1);

	char findend[XMLNODE_MAXSIZE_NAME];
	findend[0] = '<';
	findend[1] = '/';
	strncpy(findend+2, name, *name_size);
	findend[*name_size+2] = '>';
	findend[*name_size+3] = 0;
	tmp += *name_size+1;

	char *getend = strstr(tmp, findend);
	if (!getend)
	{
		*name_size = 0;
		*value_size = 0;
		*end = 0;
		return 0;
	}

	*value_size = getend-tmp;
	if (!value_cap || *value_size >= value_cap)
	{
		*name_size = 0;
		*value_size = 0;
		*end = 0;
		return 0;
	}
	strlcpy(value, tmp, *value_size+1);

	*end = getend-str+*name_size+1;
	return 1;
}
