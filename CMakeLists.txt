cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(Scanner)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
include(AddLLVM)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(pcg-cpp/include)
set(LLVM_LINK_COMPONENTS core support irreader irprinter analysis instcombine passes targetparser)
add_llvm_executable(strcmp PARTIAL_SOURCES_INTENDED strcmp.cpp)
add_llvm_executable(vectorizer PARTIAL_SOURCES_INTENDED vectorizer.cpp)
add_llvm_executable(instcombine-gen PARTIAL_SOURCES_INTENDED instcombine-gen.cpp)
add_llvm_executable(deadcode PARTIAL_SOURCES_INTENDED deadcode.cpp)
add_llvm_executable(upgrade-constexpr PARTIAL_SOURCES_INTENDED upgrade-constexpr.cpp)
add_llvm_executable(scan96625 PARTIAL_SOURCES_INTENDED scan96625.cpp)
add_llvm_executable(daggrep PARTIAL_SOURCES_INTENDED daggrep.cpp)
add_llvm_executable(normalizer PARTIAL_SOURCES_INTENDED normalizer.cpp)
add_llvm_executable(lutscan PARTIAL_SOURCES_INTENDED lutscan.cpp)
add_llvm_executable(strchr PARTIAL_SOURCES_INTENDED strchr.cpp)
add_llvm_executable(libcall PARTIAL_SOURCES_INTENDED libcall.cpp)
add_llvm_executable(state PARTIAL_SOURCES_INTENDED state.cpp)
