# Use FreeBSD pkg tools instead of Conan Center autotools.
# CCI flex/2.6.4 fails to bootstrap on FreeBSD 14.
# CCI autoconf/2.71 calls `/usr/bin/env m4` and hits BSD m4 in base.
#
# libpq 17.x uses Meson; Clang 19's has_function() treats
# CRYPTO_new_ex_data as a missing __builtin_* and aborts configure.
# 16.14 is the last autotools recipe and still builds with OpenSSL.
[replace_requires]
libpq/*: libpq/16.14

[platform_tool_requires]
flex/2.6.4
bison/3.8.2
m4/1.4.19
autoconf/2.71
automake/1.16.5
libtool/2.4.7

[conf]
tools.gnu:make_program=gmake
