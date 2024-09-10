#include "config/get.h"

void test_config()
{
    json_t *root = config_get();
    assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, root);
}
