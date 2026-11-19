# Toolchain file for SH-2 cross-compilation on macOS host
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR sh)
set(CMAKE_HOST_SYSTEM_NAME Linux)
set(CMAKE_HOST_SYSTEM_PROCESSOR x86_64)

# Cross-compiler (wrapper that strips -arch flags)
set(CMAKE_C_COMPILER /Users/celso/dev/tekken3-saturn/sh2eb-elf-gcc-wrapper)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_C_COMPILER_VERSION "13.2.0")

# LibSaturn's executable helper enables the C++ linker driver even for C-only
# programs. Use the same SH-2 toolchain so target flags do not reach host clang.
set(CMAKE_CXX_COMPILER /Users/celso/dev/saturn-tools/bin/sh2eb-elf-g++)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_VERSION "13.2.0")

# Disable macOS-specific flags
set(CMAKE_OSX_ARCHITECTURES "")
set(CMAKE_OSX_SYSROOT "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "")

# Compiler flags - must override any default flags
set(CMAKE_C_FLAGS_INIT "-m2 -mb -ffreestanding -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS "-m2 -mb -ffreestanding -ffunction-sections -fdata-sections" CACHE STRING "" FORCE)

# Linker
set(CMAKE_LINKER /Users/celso/dev/saturn-tools/bin/sh2eb-elf-ld)
set(CMAKE_AR /Users/celso/dev/saturn-tools/bin/sh2eb-elf-ar)
set(CMAKE_OBJCOPY /Users/celso/dev/saturn-tools/bin/sh2eb-elf-objcopy)
set(CMAKE_OBJDUMP /Users/celso/dev/saturn-tools/bin/sh2eb-elf-objdump)

# Find libraries in the sysroot
set(CMAKE_FIND_ROOT_PATH /Users/celso/dev/saturn-tools/sh2eb-elf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Skip compiler test
set(CMAKE_C_COMPILER_WORKS TRUE)
