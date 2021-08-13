#include <jansson.h>
int64_t config_get_intstr_json(json_t *aggregate, char *key)
{
	int64_t strintval;
	json_t *jstrintval = json_object_get(aggregate, key);
	if (jstrintval && json_typeof(jstrintval) == JSON_STRING)
		strintval = strtoull(json_string_value(jstrintval), NULL, 10);
	else
		strintval = json_integer_value(jstrintval);

	return strintval;
}

double config_get_floatstr_json(json_t *aggregate, char *key)
{
	double strintval;
	json_t *jstrintval = json_object_get(aggregate, key);
	if (jstrintval && json_typeof(jstrintval) == JSON_STRING)
		strintval = strtod(json_string_value(jstrintval), NULL);
	else
		strintval = json_real_value(jstrintval);

	return strintval;
}
