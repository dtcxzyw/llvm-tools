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

file(GLOB SOURCES *.cpp)
foreach(SOURCE ${SOURCES})
  get_filename_component(FILENAME ${SOURCE} NAME_WE)
  add_llvm_executable(${FILENAME} PARTIAL_SOURCES_INTENDED ${SOURCE})
endforeach()
