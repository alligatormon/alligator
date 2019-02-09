#include "common/validator.h"
#define TEST_PATH_1 "/usr/local/Homebrew/Library/Homebrew/vendor/bundle-standalone/ruby/2.3.0/gems/concurrent-ruby-1.1.4/lib/concurrent/collection/map/mri_map_backend.rb"
#define TEST_PATH_2 "/usr/local/Homebrew/Library/Homebrew/.rubocop.yml"
#define TEST_PATH_3 "/usr/local/Homebrew/Library/Homebrew/shims/linux/super/g++-4.9"
#define TEST_PATH_4 "/@usr/l~oc§a`l/;H]ome[br_ew/L}i{brary/Ho:b\\rew/s\"hi'ms/linux/super/g++-4.9"
#define TEST_PATH_5 "/usr/bin/c++"
#define TEST_DOMAIN_1 "example.com"
#define TEST_DOMAIN_2 "EXAMPLE.COM"
#define TEST_DOMAIN_3 "e-xample.net"
#define TEST_DOMAIN_4 "e_xample.org"
#define TEST_DOMAIN_5 "e_xam@ple.org"
#define TEST_DOMAIN_6 "e_xam=ple.org"
#define TEST_DOMAIN_7 "oofi0Ceiyep2ie.taDei0oong8pe9egh7oid.airu7ingih7sheewee1zohh.aaf8Haing2kah6eiBeeDee99Eighi.evuashohkec6se5oug9.Iyee7eis9eej5peo2wie.s4reix8oos7ae1i.eshohwaneij1melaech.ohmeiPiethaes2ookoo.8eixuuwaeshe4uikohv6j.ei2eo7wietohcim9shu.2kuj0Ieneituuxu.a5shee.ki"
#define TEST_DOMAIN_8 "oofi0Ceiykep2ie.taDei0oong8pe9egh7oid.airu7ingih7sheewee1zohh.aaf8Haing2kah6eiBeeDee99Eighi.evuashohkec6se5oug9.Iyee7eis9eej5peo2wie.s4reix8oos7ae1i.eshohwaneij1melaech.ohmeiPiethaes2ookoo.8eixuuwaeshe4uikohv6j.ei2eo7wietohcim9shu.2kuj0Ieneituuxu.a5shee.ki"
#define TEST_DOMAIN_9 "cei8aiw2ohm7MahPooch7ohz9nah4koh9nocai6ahtheizeesheeqeija2quiex.com"
#define TEST_DOMAIN_10 "cei8aiw2ohm7MahPooch7ohz9nah4koh9nocai6ahtheizeesheequeija2quiex.com"

void test_validate_path_1()
{
	int rc = validate_path(TEST_PATH_1, strlen(TEST_PATH_1));
	cut_assert_equal_int(1, rc);
}
void test_validate_path_2()
{
	int rc = validate_path(TEST_PATH_2, strlen(TEST_PATH_2));
	cut_assert_equal_int(1, rc);
}
void test_validate_path_3()
{
	int rc = validate_path(TEST_PATH_3, strlen(TEST_PATH_3));
	cut_assert_equal_int(1, rc);
}
void test_validate_path_4()
{
	int rc = validate_path(TEST_PATH_4, strlen(TEST_PATH_4));
	cut_assert_equal_int(1, rc);
}
void test_validate_path_5()
{
	int rc = validate_path(TEST_PATH_5, strlen(TEST_PATH_5));
	cut_assert_equal_int(1, rc);
}

void test_validate_domainname_1()
{
	int rc = validate_domainname(TEST_DOMAIN_1, strlen(TEST_DOMAIN_1));
	cut_assert_equal_int(1, rc);
}
void test_validate_domainname_2()
{
	int rc = validate_domainname(TEST_DOMAIN_2, strlen(TEST_DOMAIN_2));
	cut_assert_equal_int(1, rc);
}
void test_validate_domainname_3()
{
	int rc = validate_domainname(TEST_DOMAIN_3, strlen(TEST_DOMAIN_3));
	cut_assert_equal_int(1, rc);
}
void test_validate_domainname_4()
{
	int rc = validate_domainname(TEST_DOMAIN_4, strlen(TEST_DOMAIN_4));
	cut_assert_equal_int(0, rc);
}
void test_validate_domainname_5()
{
	int rc = validate_domainname(TEST_DOMAIN_5, strlen(TEST_DOMAIN_5));
	cut_assert_equal_int(0, rc);
}
void test_validate_domainname_6()
{
	int rc = validate_domainname(TEST_DOMAIN_6, strlen(TEST_DOMAIN_6));
	cut_assert_equal_int(0, rc);
}
void test_validate_domainname_7()
{
	int rc = validate_domainname(TEST_DOMAIN_7, strlen(TEST_DOMAIN_7));
	cut_assert_equal_int(1, rc);
}
void test_validate_domainname_8()
{
	int rc = validate_domainname(TEST_DOMAIN_8, strlen(TEST_DOMAIN_8));
	cut_assert_equal_int(0, rc);
}
void test_validate_domainname_9()
{
	int rc = validate_domainname(TEST_DOMAIN_9, strlen(TEST_DOMAIN_9));
	cut_assert_equal_int(1, rc);
}
void test_validate_domainname_10()
{
	int rc = validate_domainname(TEST_DOMAIN_10, strlen(TEST_DOMAIN_10));
	cut_assert_equal_int(0, rc);
}
