from conan import ConanFile
from conan.tools.cmake import cmake_layout


class AlligatorConan(ConanFile):
	settings = "os", "compiler", "build_type", "arch"
	generators = "CMakeDeps", "CMakeToolchain"
	default_options = {
		"pcre/*:with_jit": True,
		"libpq/*:with_openssl": True,
		"libpq/*:shared": False,
		"mariadb-connector-c/*:with_curl": False,
		"openssl/*:shared": False,
		"libmaxminddb/*:shared": False,
		"elfutils/*:shared": False,
		"elfutils/*:with_bzlib": False,
		"elfutils/*:with_lzma": False,
		"elfutils/*:with_zstd": False,
		"elfutils/*:debuginfod": False,
		"elfutils/*:libdebuginfod": False,
	}

	def layout(self):
		cmake_layout(self)

	def requirements(self):
		self.requires("libuv/1.44.2")
		self.requires("jansson/2.13.1")
		self.requires("jemalloc/5.3.0")
		self.requires("libuuid/1.0.3")
		self.requires("pcre/8.45")
		self.requires("libpq/17.7")
		self.requires("openssl/3.2.2")
		self.requires("xxhash/0.8.2")
		self.requires("zlib/1.3.1")
		self.requires("librdkafka/2.14.2")
		self.requires("libmaxminddb/1.12.2")
		# libbpf needs libelf; Conan Center elfutils does not support macOS.
		if self.settings.os == "Linux":
			self.requires("elfutils/0.195")
