from conans import ConanFile
from platform import system

class Deps(ConanFile):
    name = "dependencies"

    def build(self):
        if system() != "FreeBSD":
            self.run("cd external/iptables && ./autogen.sh && ./configure --disable-nftables --enable-static && make -j && make -j install")
        if system() != "FreeBSD":
            self.run("cd external/libfyaml && sh bootstrap.sh && ./configure && make -j && make -j install")
        if system() != "FreeBSD":
            self.run("cd external/libatasmart && ./autogen.sh && ./configure --disable-nftables --enable-static && make -j && make -j install")
        self.run("cd external/mruby && rake && make -j")
