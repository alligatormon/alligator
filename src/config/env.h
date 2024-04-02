typedef struct env_tree {
	uint8_t active;
	struct env_tree *steam;
	struct env_tree *next;
	struct env_tree *tail;
	char *name;
	uint8_t is_array;
	uint64_t allocated;
	void *value;
} env_tree;
void parse_env(char **envp);
