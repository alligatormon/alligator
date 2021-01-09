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
		if (str[i] == '<' && str[i+1] != '/')
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
	strlcpy(name, tmp, *name_size+1);

	char findend[XMLNODE_MAXSIZE_NAME];
	findend[0] = '<';
	findend[1] = '/';
	findend[*name_size] = '>';
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
	strlcpy(value, tmp, *value_size+1);

	*end = getend-str+*name_size+1;
	return 1;
}
