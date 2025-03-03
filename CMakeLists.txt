cmake_minimum_required(VERSION 3.20)

project(jewena_patch)

if (MSVC)
    add_compile_options(/utf-8)
endif()

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

set(DETOURS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/detours.lib")

set(ENABLE_ICONV OFF CACHE BOOL "Libiconv is not needed.")
add_subdirectory(utils)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/utils")

set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "CommonCrypto is not needed.")
set(ENABLE_GNUTLS OFF CACHE BOOL "GnuTLS is not needed.")
set(ENABLE_MBEDTLS OFF CACHE BOOL "MbedTLS is not needed.")
set(ENABLE_OPENSSL OFF CACHE BOOL "OpenSSL is not needed.")
set(ENABLE_WINDOWS_CRYPTO OFF CACHE BOOL "Schannel is not needed.")

set(ENABLE_BZIP2 OFF CACHE BOOL "Bzip2 is not needed.")
set(ENABLE_LZMA OFF CACHE BOOL "Lzma is not needed.")
set(ENABLE_ZSTD OFF CACHE BOOL "Zstd is not needed.")

set(BUILD_TOOLS OFF CACHE BOOL "Tools are not needed.")
set(BUILD_REGRESS OFF CACHE BOOL "Tests are not needed.")
set(BUILD_OSSFUZZ OFF CACHE BOOL "Fuzz tests are not needed.")
set(BUILD_EXAMPLES OFF CACHE BOOL "Examples are not needed.")
set(BUILD_DOC OFF CACHE BOOL "Documentation is not needed.")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static library is needed.")
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "Installation is not needed.")
set(ZLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Zlib is needed.")

find_package(ZLIB REQUIRED)
add_subdirectory("libzip")

add_library(jewena_patch SHARED dllmain.cpp config.hpp config.cpp vfs.hpp vfs.cpp string_replace_file.hpp string_replace_file.cpp)
target_link_libraries(jewena_patch "${DETOURS_LIB}")
target_link_libraries(jewena_patch utils)
target_link_libraries(jewena_patch zip)
target_link_libraries(jewena_patch shlwapi.lib)

add_executable(jewena-chs WIN32 main.cpp winres.rc jewena-chs.exe.manifest)
