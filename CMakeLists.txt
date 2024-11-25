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

add_library(jewena_patch SHARED dllmain.cpp)
target_link_libraries(jewena_patch "${DETOURS_LIB}")
target_link_libraries(jewena_patch utils)

add_executable(jewena-chs WIN32 main.cpp winres.rc jewena-chs.exe.manifest)
