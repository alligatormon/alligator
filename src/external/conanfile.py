from conans import ConanFile
from platform import system

class Deps(ConanFile):
    name = "dependencies"

    def build(self):
        if system() != "FreeBSD":
            self.run("cd external/libfyaml && sh bootstrap.sh && ./configure && make -j4 && make -j4 install")
        if system() != "FreeBSD":
            self.run("cd external/libatasmart && ./autogen.sh && ./configure --disable-nftables --enable-static && make -j4 && make -j4 install")
        self.run("cd external/mruby && rake && make -j4")
        self.run("cd external/libhv && ./configure --with-openssl --with-nghttp2 && make -j4")
        self.run("cd external/mbedtls/ && cmake . && make -j4 install")
        self.run("cd external/zookeeper && mvn clean install -DskipTests -Pfull-build && cd zookeeper-client/zookeeper-client-c/ && ./configure && make -j4 install")
