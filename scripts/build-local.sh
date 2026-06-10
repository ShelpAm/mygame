#!/bin/bash
# Cross-compile SDL3 for MinGW-w64 and install into the mingw sysroot.
# Run once. Requires: cmake, ninja, mingw64-gcc-c++

rm build -r

conan install . -b missing -pr default

cmake --preset conan-release
cmake --build --preset conan-release
