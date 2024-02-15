import os
from conan import ConanFile
from conan.tools.files import copy
from conan.tools.cmake import CMake

class Linenoise(ConanFile):
    name = "linenoise"
    version = "1.0"
    homepage = "https://github.com/antirez/linenoise"
    license = "BSD-2-Clause"

    options = {
        'fPIC': [True, False],
        'shared': [True, False]
    }

    default_options = {
        'fPIC': True,
        'shared': False
    }

    settings = 'os', 'compiler', 'arch', 'build_type'
    generators = ['CMakeToolchain']
    exports_sources = [
        'linenoise.c',
        'linenoise.h',
        'LICENSE',
        'CMakeLists.txt'
    ]

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, 'LICENSE', src=self.build_folder, dst=self.package_folder)