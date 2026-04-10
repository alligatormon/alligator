#include "dstructures/maglev.h"

void test_maglev_core_paths()
{
    maglev_lookup_hash m;
    maglev_init(&m);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, m.is_use_index);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, m.is_modify_lock);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, is_maglev_prime(200));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, is_maglev_prime(40010));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, is_maglev_prime(211));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, is_maglev_prime(213));

    /* add before update should be rejected */
    int node_info_a = 101;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -2, maglev_add_node(&m, "node-a", &node_info_a));

    /* invalid bucket size -> update fails */
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, maglev_update_service(&m, 2, 212));

    maglev_init(&m);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, maglev_update_service(&m, 2, 211));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, m.is_modify_lock);
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, m.p_temp);

    int node_info_b = 202;
    int node_info_c = 303;
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, maglev_add_node(&m, "node-b", &node_info_b));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, maglev_add_node(&m, "node-c", &node_info_c));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, -1, maglev_add_node(&m, "node-overflow", &node_info_a));

    maglev_create_ht(&m);
    maglev_swap_entry(&m);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, m.is_modify_lock);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, m.is_use_index);

    void *picked = maglev_lookup_node(&m, "client-key", (int)strlen("client-key"));
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, picked == &node_info_b || picked == &node_info_c);

    /* lookup on uninitialized object */
    maglev_lookup_hash m2;
    maglev_init(&m2);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, maglev_lookup_node(&m2, "k", 1) != NULL);

    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, DJBHash("abc") > 0);
    assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, murmur2("abcd", 4) > 0);

    maglev_loopup_item_clean(&m, m.is_use_index);
}
