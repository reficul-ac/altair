from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class AnimusRecipe(ConanFile):
    name = "animus"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    default_options = {
        "gdal/*:with_arrow": False,
        "gdal/*:gdal_optional_drivers": False,
        "gdal/*:ogr_optional_drivers": False,
        "gdal/*:with_curl": False,
        "gdal/*:with_geos": False,
        "gdal/*:with_gif": False,
        "gdal/*:with_jpeg": False,
        "gdal/*:with_lerc": False,
        "gdal/*:with_opencl": False,
        "gdal/*:with_png": False,
        "gdal/*:with_qhull": False,
        "gdal/*:with_sqlite3": True,
        "gdal/*:tools": False,
        "libtiff/*:jpeg": False,
        "proj/*:build_executables": False,
        "proj/*:with_curl": False,
        "sqlite3/*:enable_column_metadata": True,
    }

    def requirements(self):
        self.requires("glfw/3.4")
        self.requires("glew/2.2.0")
        self.requires("gtest/1.17.0")
        self.requires("libpng/1.6.58")
        self.requires("gdal/3.12.1")

    def generate(self):
        CMakeToolchain(self).generate()
        CMakeDeps(self).generate()
