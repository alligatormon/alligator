#include "common/pw.h"
void test_pw()
{
	cut_assert_equal_string("root", get_username_by_uid(0));
	cut_assert_equal_string("root", get_groupname_by_gid(0));
	cut_assert_equal_int(0, get_uid_by_username("root"));
	cut_assert_equal_int(0, get_gid_by_groupname("root"));
}

