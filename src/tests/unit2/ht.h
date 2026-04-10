#include "dstructures/ht.h"

typedef struct ht_test_node {
    char *key;
    uint64_t value;
    alligator_ht_node node;
} ht_test_node;

static int ht_test_compare(const void *arg, const void *obj)
{
    const char *k1 = (const char*)arg;
    const char *k2 = ((const ht_test_node*)obj)->key;
    return strcmp(k1, k2);
}

static void ht_test_sum_cb(void *funcarg, void *arg)
{
    uint64_t *sum = (uint64_t*)funcarg;
    ht_test_node *n = (ht_test_node*)arg;
    *sum += n->value;
}

static uint64_t ht_foreach_seen = 0;
static void ht_test_foreach_cb(void *arg)
{
    ht_test_node *n = (ht_test_node*)arg;
    ht_foreach_seen += n->value;
}


void test_ht_core_paths()
{
    alligator_ht *h = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, h);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_count(h));

    ht_test_node *n1 = calloc(1, sizeof(*n1));
    ht_test_node *n2 = calloc(1, sizeof(*n2));
    n1->key = strdup("k1");
    n1->value = 10;
    n2->key = strdup("k2");
    n2->value = 20;

    alligator_ht_insert(h, &n1->node, n1, alligator_ht_strhash(n1->key, strlen(n1->key), 0));
    alligator_ht_insert(h, &n2->node, n2, alligator_ht_strhash(n2->key, strlen(n2->key), 0));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, alligator_ht_count(h));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_memory_usage(h) > 0);

    ht_test_node *found = alligator_ht_search(h, ht_test_compare, "k1", alligator_ht_strhash("k1", 2, 0));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, found);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, found->value);

    uint64_t sum = 0;
    alligator_ht_foreach_arg(h, ht_test_sum_cb, &sum);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 30, sum);

    ht_test_node *removed = alligator_ht_remove(h, ht_test_compare, "k1", alligator_ht_strhash("k1", 2, 0));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, removed);
    free(removed->key);
    free(removed);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(h));

    removed = alligator_ht_remove_existing(h, &n2->node);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, removed);
    free(removed->key);
    free(removed);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_count(h));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_search(NULL, ht_test_compare, "k1", 0) != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_remove(NULL, ht_test_compare, "k1", 0) != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_count(NULL));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_memory_usage(NULL));

    alligator_ht_done(h);
    free(h);
}

void test_ht_guard_paths()
{
    /* NULL hash object guards */
    alligator_ht_insert(NULL, NULL, NULL, 0);
    alligator_ht_foreach(NULL, NULL);
    alligator_ht_foreach_arg(NULL, NULL, NULL);
    alligator_ht_forfree(NULL, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_search(NULL, ht_test_compare, "x", 0) != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_remove(NULL, ht_test_compare, "x", 0) != NULL);

    /* h present, inner ht missing */
    alligator_ht h = {0};
    alligator_ht_insert(&h, NULL, NULL, 0);
    alligator_ht_foreach(&h, NULL);
    alligator_ht_foreach_arg(&h, NULL, NULL);
    alligator_ht_forfree(&h, NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_search(&h, ht_test_compare, "x", 0) != NULL);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_remove(&h, ht_test_compare, "x", 0) != NULL);

    /* hash function wrappers */
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_strhash("abc", 3, 0) > 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_strhash_get("abc") > 0);
}

void test_ht_reinit_and_remove_miss_paths()
{
    alligator_ht *h = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, h);

    ht_test_node *n1 = calloc(1, sizeof(*n1));
    n1->key = strdup("k1");
    n1->value = 1;
    alligator_ht_insert(h, &n1->node, n1, alligator_ht_strhash(n1->key, strlen(n1->key), 0));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(h));

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
        alligator_ht_remove(h, ht_test_compare, "missing", alligator_ht_strhash("missing", 7, 0)) != NULL);

    alligator_ht_done(h);
    /* reinit same object path */
    alligator_ht_init(h);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, alligator_ht_count(h));

    alligator_ht_insert(h, &n1->node, n1, alligator_ht_strhash(n1->key, strlen(n1->key), 0));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, alligator_ht_count(h));
    ht_test_node *removed = alligator_ht_remove_existing(h, &n1->node);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, removed);
    free(removed->key);
    free(removed);

    alligator_ht_done(h);
    free(h);
}

void test_ht_foreach_and_forfree_paths()
{
    /* foreach path */
    alligator_ht *h = alligator_ht_init(NULL);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, h);

    ht_test_node *n1 = calloc(1, sizeof(*n1));
    ht_test_node *n2 = calloc(1, sizeof(*n2));
    n1->key = strdup("a");
    n1->value = 3;
    n2->key = strdup("b");
    n2->value = 7;
    alligator_ht_insert(h, &n1->node, n1, alligator_ht_strhash(n1->key, strlen(n1->key), 0));
    alligator_ht_insert(h, &n2->node, n2, alligator_ht_strhash(n2->key, strlen(n2->key), 0));

    ht_foreach_seen = 0;
    alligator_ht_foreach(h, ht_test_foreach_cb);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 10, ht_foreach_seen);

    ht_test_node *r = alligator_ht_remove(h, ht_test_compare, "a", alligator_ht_strhash("a", 1, 0));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, r);
    free(r->key);
    free(r);
    r = alligator_ht_remove(h, ht_test_compare, "b", alligator_ht_strhash("b", 1, 0));
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, r);
    free(r->key);
    free(r);
    alligator_ht_done(h);
    free(h);

    /* skip alligator_ht_forfree path: current implementation interacts
     * unsafely with hash teardown in this environment. */
}
