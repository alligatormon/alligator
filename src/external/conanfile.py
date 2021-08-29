from conans import ConanFile

class Deps(ConanFile):
    name = "dependencies"

    def build(self):
        self.run("cd external/iptables && ./autogen.sh && ./configure --disable-nftables --enable-static && make -j && make -j install")
        self.run("cd external/libfyaml && ./bootstrap.sh && ./configure && make -j && make -j install")
        self.run("cd external/libatasmart && ./autogen.sh && ./configure --disable-nftables --enable-static && make -j && make -j install")
        self.run("cd external/mruby && rake && make -j")
