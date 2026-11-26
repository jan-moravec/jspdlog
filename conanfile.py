"""Conan recipe that packages the headers of this checkout. Test it with:

conan create . --test-folder packaging/conan/test_package --build=missing -s compiler.cppstd=17
"""

import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.files import copy

required_conan_version = ">=2.0"


class JspdlogConan(ConanFile):
    name = "jspdlog"
    version = "0.5.0"
    license = "MIT"
    url = "https://github.com/jan-moravec/jspdlog"
    homepage = "https://github.com/jan-moravec/jspdlog"
    description = "JSON-lines logging for C++ with typed fields and explicit context, on spdlog's sinks."
    topics = ("logging", "json", "spdlog", "header-only")
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "include/*", "LICENSE"
    no_copy_source = True

    def requirements(self):
        self.requires("spdlog/[>=1.12 <2]", transitive_headers=True)

    def validate(self):
        check_min_cppstd(self, 17)

    def package_id(self):
        self.info.clear()

    def package(self):
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "*.h", os.path.join(self.source_folder, "include"), os.path.join(self.package_folder, "include"))

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "jspdlog")
        self.cpp_info.set_property("cmake_target_name", "jspdlog::jspdlog")
        # The header reads the text policy in every translation unit, so the package fixes it for all its consumers.
        self.cpp_info.defines = ["JSPDLOG_UTF8_POLICY=JSPDLOG_UTF8_REPLACE"]
