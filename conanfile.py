from conan import ConanFile
from conan.tools.cmake import cmake_layout


class TheSunsetStraits(ConanFile):
    name = "TheSunsetStraits"
    version = "0.1.0"

    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        assert self.requires is not None
        self.requires("sdl/3.4.8")
        self.requires("boost/1.90.0")
        self.requires("imgui/1.91.8")

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

    def layout(self):
        cmake_layout(self)
