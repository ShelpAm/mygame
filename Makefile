# Cross-compile SDL3 for MinGW-w64 and install into the mingw sysroot.
# Run once. Requires: cmake, ninja, mingw64-gcc-c++

# Targets
TARGET = TheSunsetStraits.exe
BUILD_DIR = build/Release
ASSETS_DIR = assets
TARBALL = $(BUILD_DIR)/thesunsetstraits.tar.gz

# Default target
all: clean build package

# Clean build directory
clean:
	rm -rf $(BUILD_DIR)

# Install dependencies with Conan
conan:
	conan install . -b missing -s build_type=Release -pr mingw64

# Configure with CMake
configure: conan
	cmake --preset conan-release

# Build the project
build: configure
	cmake --build --preset conan-release

# Package the executable and assets
package: build
	tar czf $(TARBALL) -C $(BUILD_DIR) $(TARGET) -C $(PWD) assets

# Phony targets (not actual files)
.PHONY: all configure build package

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Clean, build, and package (default)"
	@echo "  clean     - Remove build directory"
	@echo "  conan     - Install dependencies"
	@echo "  configure - Run CMake configuration"
	@echo "  build     - Build the project"
	@echo "  package   - Create tarball"
