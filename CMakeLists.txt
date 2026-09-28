cmake_minimum_required(VERSION 3.20)

project(wc2r_plus VERSION 1.0.0 LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "wc2r-plus must be built for 32-bit Windows (Warcraft II Remastered is x86): "
                        "configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        d94c64d32ea37bc4f5ee47d580709f70c6fb6080
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(minhook)

configure_file(src/wc2r_version.h.in ${CMAKE_BINARY_DIR}/generated/wc2r_version.h @ONLY)
configure_file(src/version.rc.in ${CMAKE_BINARY_DIR}/generated/version.rc @ONLY)

add_subdirectory(src)
add_subdirectory(installer)
