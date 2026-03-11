from conans import ConanFile
from platform import system

class Deps(ConanFile):
    name = "dependencies"

    def build(self):
        if system() != "FreeBSD":
            self.run("cd external/libfyaml && find . -type f -exec sed -i -e 's/XXH/libfyaml_XXH/g'  -e 's/libfyaml_libfyaml_XXH/libfyaml_XXH/g' {} \; && sh bootstrap.sh && autoreconf -f && autoreconf -i -f && ./configure && make -j4 && make -j4")
        if system() != "FreeBSD":
            self.run("cd external/libatasmart && ./autogen.sh && ./configure --enable-static && make -j4 && make -j4")
        self.run("cd external/mruby && make")
        self.run("cd external/libhv && ./configure --without-http-server --without-http-client --without-http --without-evpp && make libhv")
        self.run("cd external/libhv && cc -c -Iinclude/hv/ -std=c99 -o libdns.o protocol/dns.c && ar rcs lib/libdns.a libdns.o")
        self.run("cd external/picohttpparser && cc -c -o libpicohttpparser.o picohttpparser.c && ar rcs libpicohttpparser.a libpicohttpparser.o")
