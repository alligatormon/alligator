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
#define TEST_DOMAIN_11 "r."

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

void test_validate_domainname_11()
{
	int rc = validate_domainname(TEST_DOMAIN_11, strlen(TEST_DOMAIN_11));
	cut_assert_equal_int(0, rc);
}

#define TEST_METRIC_NAME_1 "test"
#define TEST_METRIC_NAME_2 "test_test"
#define TEST_METRIC_NAME_3 "abcdefghijklmnopqrstuvwxyz"
#define TEST_METRIC_NAME_4 "abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz"
#define TEST_METRIC_NAME_5 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define TEST_METRIC_NAME_6 "ABCDEFGHIJKLMNOPQRSTUVWXYZ_ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define TEST_METRIC_NAME_7 "abcdefghijklmnopqrst.uvwxyz"
#define TEST_METRIC_NAME_8 "abcdefghijklmnopq#rstuvwxyz"
#define TEST_METRIC_NAME_9 "abcdefghijklmnopq!rstuvwxyz"
#define TEST_METRIC_NAME_10 "abcde@fghijklmnopqrstuvwxyz"
#define TEST_METRIC_NAME_11 "abcdefghij$klmnopqrstuvwxyz"
#define TEST_METRIC_NAME_12 "# abcdefghijklmnopqrstuvwxyz"
#define TEST_METRIC_NAME_13 " abcdefghijklmnopqrstuvwxyz"
#define TEST_METRIC_NAME_14 "abcdefghijk\tlmnopqrstuvwxyz"
#define TEST_METRIC_NAME_15 "abcdefghijklmnopqrstuvwxyz\n"
#define TEST_METRIC_NAME_16 "abcdefghijklmnopqrstuvwxyz\r"
#define TEST_METRIC_NAME_17 "abcdefghijklmnopqrstuvw*xyz"
#define TEST_METRIC_NAME_18 "abcdefghijklmnopqrstuvw(xyz"
#define TEST_METRIC_NAME_19 "abcdefghijklmnopqrstuvw)xyz"
#define TEST_METRIC_NAME_20 "abcdefghiklmnopqrstuvw-xyz"
#define TEST_METRIC_NAME_21 "abcdefghijklmnopqrstuvw+xyz"
#define TEST_METRIC_NAME_22 "abcdefghijklmnopqrstuvw%%xyz"
#define TEST_METRIC_NAME_23 "abcdefghijklmnopqrstuv^wxyz"
#define TEST_METRIC_NAME_24 "abcdefghijklmnopqrstuv&wxyz"
#define TEST_METRIC_NAME_25 "abcdefghijklmnopqrstuv,wxyz"
#define TEST_METRIC_NAME_26 "abcdefghijklmnopqrstuv§wxyz"
#define TEST_METRIC_NAME_27 "abcdefghijklmnopqrstuv/wxyz"
#define TEST_METRIC_NAME_28 "abcdefghijklmnopqrstuv{wxyz"
#define TEST_METRIC_NAME_29 "abcdefghijklmnopqrstuv[wxyz"
#define TEST_METRIC_NAME_30 "abcdefghijklmnopqrstuv]wxyz"
#define TEST_METRIC_NAME_31 "abcdefghijklmnopqrstuv|wxyz"
#define TEST_METRIC_NAME_32 "abcdefghijklmnopqrstuv\\wxyz"
#define TEST_METRIC_NAME_33 "abcdefghijklmnopqrstuv;wxyz"
#define TEST_METRIC_NAME_34 "abcdefghijklmnopqrstuv'wxyz"
#define TEST_METRIC_NAME_35 "abcdefghijklmnopqrstuv\"wxyz"
#define TEST_METRIC_NAME_36 "abcdefghijklmnopqrstuvwxyz_1234567890"
#define TEST_METRIC_NAME_37 "0"
void test_metric_name_validator_1()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_1, strlen(TEST_METRIC_NAME_1));
	cut_assert_equal_int(1, rc);
}
void test_metric_name_validator_2()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_2, strlen(TEST_METRIC_NAME_2));
	cut_assert_equal_int(1, rc);
}
void test_metric_name_validator_3()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_3, strlen(TEST_METRIC_NAME_3));
	cut_assert_equal_int(1, rc);
}
void test_metric_name_validator_4()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_4, strlen(TEST_METRIC_NAME_4));
	cut_assert_equal_int(1, rc);
}
void test_metric_name_validator_5()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_5, strlen(TEST_METRIC_NAME_5));
	cut_assert_equal_int(1, rc);
}
void test_metric_name_validator_6()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_6, strlen(TEST_METRIC_NAME_6));
	cut_assert_equal_int(1, rc);
}
void test_metric_name_validator_7()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_7, strlen(TEST_METRIC_NAME_7));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_8()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_8, strlen(TEST_METRIC_NAME_8));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_9()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_9, strlen(TEST_METRIC_NAME_9));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_10()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_10, strlen(TEST_METRIC_NAME_10));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_11()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_11, strlen(TEST_METRIC_NAME_11));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_12()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_12, strlen(TEST_METRIC_NAME_12));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_13()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_13, strlen(TEST_METRIC_NAME_13));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_14()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_14, strlen(TEST_METRIC_NAME_14));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_15()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_15, strlen(TEST_METRIC_NAME_15));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_16()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_16, strlen(TEST_METRIC_NAME_16));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_17()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_17, strlen(TEST_METRIC_NAME_17));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_18()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_18, strlen(TEST_METRIC_NAME_18));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_19()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_19, strlen(TEST_METRIC_NAME_19));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_20()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_20, strlen(TEST_METRIC_NAME_20));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_21()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_21, strlen(TEST_METRIC_NAME_21));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_22()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_22, strlen(TEST_METRIC_NAME_22));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_23()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_23, strlen(TEST_METRIC_NAME_23));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_24()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_24, strlen(TEST_METRIC_NAME_24));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_25()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_25, strlen(TEST_METRIC_NAME_25));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_26()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_26, strlen(TEST_METRIC_NAME_26));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_27()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_27, strlen(TEST_METRIC_NAME_27));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_28()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_28, strlen(TEST_METRIC_NAME_28));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_29()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_29, strlen(TEST_METRIC_NAME_29));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_30()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_30, strlen(TEST_METRIC_NAME_30));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_31()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_31, strlen(TEST_METRIC_NAME_31));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_32()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_32, strlen(TEST_METRIC_NAME_32));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_33()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_33, strlen(TEST_METRIC_NAME_33));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_34()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_34, strlen(TEST_METRIC_NAME_34));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_35()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_35, strlen(TEST_METRIC_NAME_35));
	cut_assert_equal_int(0, rc);
}
void test_metric_name_validator_36()
{
	int rc = metric_name_validator(TEST_METRIC_NAME_36, strlen(TEST_METRIC_NAME_36));
	cut_assert_equal_int(1, rc);
}

// metric validator
#define TEST_VALIDATOR_VALUE_1 "0"
#define TEST_VALIDATOR_VALUE_2 "0.0"
#define TEST_VALIDATOR_VALUE_3 ".0"
#define TEST_VALIDATOR_VALUE_4 ".013123124"
#define TEST_VALIDATOR_VALUE_5 ".01312312.4"
#define TEST_VALIDATOR_VALUE_6 ".013123124."
#define TEST_VALIDATOR_VALUE_7 ".0131231#24"
#define TEST_VALIDATOR_VALUE_8 "!.013123124"
#define TEST_VALIDATOR_VALUE_9 "013123124"
#define TEST_VALIDATOR_VALUE_10 "131&23124"
#define TEST_VALIDATOR_VALUE_11 "31891324934"
#define TEST_VALIDATOR_VALUE_12 "31891324934t"
#define TEST_VALIDATOR_VALUE_13 "-1891324934"
#define TEST_VALIDATOR_VALUE_14 "-189.1324934"
void test_metric_value_validator_1()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_1, strlen(TEST_VALIDATOR_VALUE_1));
	cut_assert_equal_int(2, rc);
}
void test_metric_value_validator_2()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_2, strlen(TEST_VALIDATOR_VALUE_2));
	cut_assert_equal_int(1, rc);
}

void test_metric_value_validator_3()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_3, strlen(TEST_VALIDATOR_VALUE_3));
	cut_assert_equal_int(1, rc);
}
void test_metric_value_validator_4()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_4, strlen(TEST_VALIDATOR_VALUE_4));
	cut_assert_equal_int(1, rc);
}
void test_metric_value_validator_5()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_5, strlen(TEST_VALIDATOR_VALUE_5));
	cut_assert_equal_int(0, rc);
}
void test_metric_value_validator_6()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_6, strlen(TEST_VALIDATOR_VALUE_6));
	cut_assert_equal_int(0, rc);
}
void test_metric_value_validator_7()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_7, strlen(TEST_VALIDATOR_VALUE_7));
	cut_assert_equal_int(0, rc);
}
void test_metric_value_validator_8()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_8, strlen(TEST_VALIDATOR_VALUE_8));
	cut_assert_equal_int(0, rc);
}
void test_metric_value_validator_9()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_9, strlen(TEST_VALIDATOR_VALUE_9));
	cut_assert_equal_int(2, rc);
}
void test_metric_value_validator_10()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_10, strlen(TEST_VALIDATOR_VALUE_10));
	cut_assert_equal_int(0, rc);
}
void test_metric_value_validator_11()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_11, strlen(TEST_VALIDATOR_VALUE_11));
	cut_assert_equal_int(2, rc);
}
void test_metric_value_validator_12()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_12, strlen(TEST_VALIDATOR_VALUE_12));
	cut_assert_equal_int(0, rc);
}

void test_metric_value_validator_13()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_13, strlen(TEST_VALIDATOR_VALUE_13));
	cut_assert_equal_int(2, rc);
}

void test_metric_value_validator_14()
{
	int rc = metric_value_validator(TEST_VALIDATOR_VALUE_14, strlen(TEST_VALIDATOR_VALUE_14));
	cut_assert_equal_int(1, rc);
}
