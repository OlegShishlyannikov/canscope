set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(SYSROOT "/sysroot" CACHE PATH "Path to aarch64 sysroot")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

# Find GCC installation inside sysroot for crt files and libgcc
file(GLOB _gcc_dirs "${SYSROOT}/usr/lib/gcc/aarch64-*/*")
list(GET _gcc_dirs 0 _gcc_dir)
get_filename_component(GCC_INSTALL_DIR "${_gcc_dir}" DIRECTORY)

set(CMAKE_C_FLAGS_INIT "--gcc-install-dir=${_gcc_dir}")
set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir=${_gcc_dir}")
set(CMAKE_LINKER_TYPE LLD)

set(CMAKE_SYSROOT ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
