#define ENV_MAX_SIZE 1024
#include "main.h"
#include "config/env.h"
extern aconf *ac;

// env tree example structure
// [root]: (is_array = 0, index = not defined, name = root, value = not defined)
//   [entrypoint]:  (is_array = 1, index = not defined, name = entrypoint, value = not defined)
//     [0]: (is_array = 0, index = 0, name = not defined, value = not defined)
//       [tcp]: (is_array = 1, index = not defined, name = entrypoint, value = not defined)
//         [0]: (is_array = 0, index = 0, name = not defined, value = not defined)
//           {1111} (is_array = 0, index = 0, name = not defined, value = "1111")
//         [1]:
//           {1112}
//       [udp]:
//         [0]:
//           {8125}
//     [1]:
//       [udp]:
//         [0]:
//           {514}
//       [handler]:
//         {rsyslog-impstats}
//   [aggregate]
//     [0]:
//       [url]:
//         {tcp://localhost:1717}
//       [handler]:
//         {uwsgi}
//     [1]:
//       [url]:
//         {tcp://localhost:6379}
//       [handler]:
//         {redis}
//     [2]:
//       [url]:
//         {tcp://:admin@localhost:26379}
//       [handler]:
//         {redis}
//         

void env_tree_init_steam(env_tree* ret, size_t size)
{
	if (size > 0)
		ret->steam = /*ret->tail =*/ calloc(size, sizeof(env_tree)); // ret->head

	ret->allocated = size;
}

env_tree* env_tree_init(size_t size)
{
	env_tree *ret = calloc(1, sizeof(env_tree));
	//ret->steam = /*ret->tail =*/ calloc(size, sizeof(env_tree)); // ret->head
	env_tree_init_steam(ret, size);
	return ret;
}

//parse_env, applied: raw:ALLIGATOR__ENTRYPOINT0__TCP0=1111, clean:alligator__entrypoint0__tcp0=1111
//> parse env ctx: 'entrypoint0', name: 'entrypoint', index: '0', array: '1'
//> parse env ctx: 'tcp0', name: 'tcp', index: '0', array: '1'
//>> value is 1111
//parse_env, applied: raw:ALLIGATOR__ENTRYPOINT0__TCP=1111, clean:alligator__entrypoint0__tcp=1111
//> parse env ctx: 'entrypoint0', name: 'entrypoint', index: '0', array: '1'
//> parse env ctx: 'tcp', name: 'tcp', index: '', array: '0'
//>> value is 1111

// return ptr to next steam/subtree by selector (name) OR (is_array AND index)

env_tree *env_node_select(env_tree *node, char *name, size_t size, uint8_t is_array)
{
	if (ac->log_level > 0)
		printf(" > env_node_select: { node: '%p', name: '%s', size: '%zu'}\n", node, name, size);

	//if (node->is_array != 2 && node->is_array != is_array)
	//{
	//	if (ac->log_level > 0)
	//		printf(" > env_node_select:  error: 'array do not mismatch', expected: %"u32", actual: %"u32"\n", node->is_array, is_array);
	//	return (void*)1;
	//}

	uint32_t hash = murmurhash ((const char*)name, size, 0);
	uint32_t index = hash % node->allocated;
	uint32_t step = 0;

	if (ac->log_level > 0)
		printf(" > env_node_select: murmurhash: '%"u32"', index: '%"u32"'\n", hash, index);

	for (; index < node->allocated && &node->steam[index] && node->steam[index].active; index++)
	{
		if (!strncmp(node->steam[index].name, name, size))
		{
			if (ac->log_level > 0)
				printf(" > env_node_select: { result: : 'OK', found: '%s', hash: '%"u32"', index: '%"u32"', steps: '%"u32"'}\n", node->steam[index].name, hash, index, step);

			return &node->steam[index];
		}
		++step;
	}

	if (ac->log_level > 0)
		printf(" > env_node_select: { result: : 'not found'}\n");

	return NULL;
}

void env_node_value(env_tree *node, char *value)
{
	node->value = value;
}

env_tree *env_node_add(env_tree *node, char *name, size_t size, uint8_t is_array)
{
	uint32_t hash = murmurhash ((const char*)name, size, 0);
	uint32_t index = hash % node->allocated;
	uint32_t step = 0;

	for (; index < node->allocated && &node->steam[index] && node->steam[index].active; index++)
	{
		++step;
	}

	if (node->allocated == index)
	{
		if (ac->log_level > 0)
			printf(" > env_node_add: { result: 'error': 'allocated size if full', name: '%s'}\n", name);
		return NULL;
	}

	env_tree_init_steam(&node->steam[index], 20);
	node->steam[index].active = 1;
	node->steam[index].name = name;
	node->steam[index].is_array = is_array;

	if (ac->log_level > 0)
		printf(" > env_node_add: { result: 'CREATED', name: '%s', array: '%d', ptr: '%p', hash: '%"u32"', index: '%"u32"', step: '%"u32"'\n", name, is_array, &node->steam[index], hash, index, step);
	return &node->steam[index];
}

// function for find or create (if not exists) node with selected params
// return new env_tree node or NULL if type mismatch (object <> array operations)
env_tree* env_node_select_create(env_tree *node, char *name, size_t size, uint8_t is_array)
{
	env_tree* selected = env_node_select(node, name, size, is_array);
	if (selected)
	{
		if (selected == (void*)1)
			return NULL;
		if (!selected->name && name)
			selected->name = name;
		return selected;
	}

	selected = env_node_add(node, name, size, is_array);
	return selected;
}

json_t* env_tree_new_node_json(json_t *nroot, env_tree *node)
{
	json_t *obj;
	if (node->value)
		obj = json_string(node->value);
	else if (node->name && node->is_array)
		obj = json_array();
	else if (node->name)
		obj = json_object();
	else
		return nroot;

	json_array_object_insert(nroot, node->name, obj);
	return obj;
}

void env_node_scan(env_tree *node, int64_t indent, json_t *nroot)
{
	if (!node)
		return;

	if (node->active)
	{
		if (ac->log_level > 0)
		{
			for (int64_t i = 0; i < indent; i++)
				printf(" ");
			printf("> (%d) {%p} name is '%s', is array '%d', allocated '%"u64"', value is '%s'\n", node->active, node, node->name, node->is_array, node->allocated, (char*)node->value);
		}

		nroot = env_tree_new_node_json(nroot, node);
	}
	else if (ac->log_level > 11)
	{
		for (int64_t i = 0; i < indent; i++)
			printf(" ");
		printf("> (%d) {%p} name is '%s', is array '%d', allocated '%"u64"', value is '%s'\n", node->active, node, node->name, node->is_array, node->allocated, (char*)node->value);
	}

	++indent;
	//env_tree *ptr = node->steam;
	for (uint64_t i = 0; i < node->allocated; i++)
	{
		env_node_scan(&node->steam[i], indent, nroot);
		//ptr = ptr->next;
	}
}

void env_tree_deserialize_json(env_tree *tree)
{
	json_t *root = json_object();
	env_node_scan(tree, 0, root);

	char *dvalue = json_dumps(root, JSON_INDENT(2));
	if (ac->log_level > 11)
		puts(dvalue);
	http_api_v1(NULL, NULL, dvalue);
	free(dvalue);
	json_decref(root);
}

void env_tree_del_node(env_tree *node)
{
	if (node->value)
		free(node->value);
	else if (node->name && node->is_array)
		free(node->name);

	free(node->steam);
}

void env_node_scan_del(env_tree *node, int64_t indent)
{
	if (!node)
		return;

	++indent;
	//env_tree *ptr = node->steam;
	for (uint64_t i = 0; i < node->allocated; i++)
	{
		env_node_scan_del(&node->steam[i], indent);
		//ptr = ptr->next;
	}

	if (node->active)
	{
		if (ac->log_level > 0)
		{
			for (int64_t i = 0; i < indent; i++)
				printf(" ");
			printf("> delete (%d) {%p} name is '%s', is array '%d', allocated '%"u64"', value is '%s'\n", node->active, node, node->name, node->is_array, node->allocated, (char*)node->value);
		}

		env_tree_del_node(node);
	}
	else if (ac->log_level > 11)
	{
		for (int64_t i = 0; i < indent; i++)
			printf(" ");
		printf("> delete (%d) {%p} name is '%s', is array '%d', allocated '%"u64"', value is '%s'\n", node->active, node, node->name, node->is_array, node->allocated, (char*)node->value);
	}
}

void env_tree_free(env_tree *tree)
{
	env_node_scan_del(tree, 0);
	free(tree);
}

void parse_env(char **envp)
{
	char clear_env[ENV_MAX_SIZE];
	char *env;
	env_tree *tree = env_tree_init(20);
	tree->active = 1;
	tree->is_array = 0;
	while ((env = *envp++))
	{
		strlcpy(clear_env, env, ENV_MAX_SIZE);
		to_lower_before(clear_env, "=");

		if (strncmp(clear_env, "alligator__", 11))
		{
			if (ac->log_level > 3)
				printf("parse_env, not applied: raw:%s, clean:%s\n", env, clear_env);
			continue;
		}

		if (ac->log_level > 2)
			printf("parse_env, applied: raw:%s, clean:%s\n", env, clear_env);


		size_t ctx_size;
		size_t name_size;
		char *tmp = clear_env + 11;
		char ctx[ENV_MAX_SIZE];
		char *name;
		char index[ENV_MAX_SIZE];
		char value[ENV_MAX_SIZE];
		*value = 0;
		char *ctx_ptr;
		int is_last_word = 0;
		env_tree *node = tree;
		while (tmp)
		{
			*index = 0;
			int is_array = 0;
			ctx_ptr = strstr(tmp, "__");
			if (!ctx_ptr)
			{
				is_last_word = 1;
				ctx_ptr = strstr(tmp, "=");
				if (!ctx_ptr)
					break;
			}

			ctx_size = ctx_ptr - tmp;

			strlcpy(ctx, tmp, ctx_size + 1);
			if (ac->log_level > 3)
				printf("> parse_env ctx: %s\n", ctx);

			name_size = strcspn(ctx, "0123456789");
			name = strndup(ctx, name_size);

			if (name_size != ctx_size)
			{
				is_array = 1;
				strlcpy(index, ctx + name_size, ENV_MAX_SIZE);
			}

			if (is_last_word)
			{
				ctx_ptr += strspn(ctx_ptr, "=");
				strlcpy(value, ctx_ptr, ENV_MAX_SIZE);
			}

			if (ac->log_level > 0)
				printf("> parse env ctx: '%s', name: {'%s'}, index: '%s', array: '%d'\n", ctx, name, index, is_array);
			node = env_node_select_create(node, name, name_size, is_array);

			if (is_array)
			{
				if (ac->log_level > 0)
					printf("> parse index ctx: '%s', name: '%s', index: {'%s'}, array: '%d'\n", ctx, name, index, is_array);
				name = strdup(index);
				name_size = strlen(name);
				node = env_node_select_create(node, name, name_size, 0);
				if (!node)
					break;
			}

			tmp += ctx_size;
			if (!tmp)
				break;

			tmp += 2;
		}

		if (node)
		{
			if (ac->log_level > 0)
				printf(">> value is %s\n", value);
			env_node_value(node, strdup(value));
		}
	}
	env_tree_deserialize_json(tree);
	env_tree_free(tree);
}
