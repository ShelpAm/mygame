from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class TheSunsetStraits(ConanFile):
    name = "TheSunsetStraits"
    version = "0.1.0"

    settings = "os", "compiler", "build_type", "arch"
    generators = CMakeDeps, CMakeToolchain

    def requirements(self):
        assert self.requires is not None
        self.requires("sdl/3.4.8")
        self.requires("boost/1.90.0")
        self.requires("imgui/1.91.8")
        self.requires("spdlog/1.17.0")
        self.requires("sdl_image/3.4.0")

    def configure(self):
        assert self.options is not None
        self.options["sdl/*"].x11 = False
        self.options["sdl/*"].xcursor = False
        self.options["sdl/*"].xdbe = False
        self.options["sdl/*"].xinput = False
        self.options["sdl/*"].xfixes = False
        self.options["sdl/*"].xrandr = False
        self.options["sdl/*"].xscrnsaver = False
        self.options["sdl/*"].xshape = False
        self.options["sdl/*"].xsync = False
        self.options["*"].with_x11 = False
        self.options["boost/*"].without_cobalt = True
        self.options["boost/*"].without_stacktrace = True
        self.options["boost/*"].without_log = True
        self.options["spdlog/*"].use_std_fmt = True

        sdl_image_opts = [
            "with_avif",
            "with_jxl",
            "with_libjpeg",
            "with_libpng",
            "with_libtiff",
            "with_libwebp",
        ]
        for opt in sdl_image_opts:
            self.options["sdl_image/*"][opt] = False
        self.options["sdl_image/*"].with_libpng = True

    def layout(self):
        cmake_layout(self)
