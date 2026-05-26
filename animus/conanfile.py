from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class AnimusRecipe(ConanFile):
    name = "animus"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("glfw/3.4")
        self.requires("glew/2.2.0")
        self.requires("gtest/1.17.0")
        self.requires("libpng/1.6.58")

    def generate(self):
        CMakeToolchain(self).generate()
        CMakeDeps(self).generate()
